#import <Cocoa/Cocoa.h>
#import <WebKit/WebKit.h>
#import <QuartzCore/QuartzCore.h>
#include "browser/Browser.hpp"
#include "output/Output.hpp"
#include "include/cef_application_mac.h"
#include "include/wrapper/cef_helpers.h"
#include "include/wrapper/cef_library_loader.h"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <iostream>
#include <unordered_map>
#include <cmath>
#include <algorithm>
using namespace u2k;
static bool smoke=false,hardwareTest=false,noDeviceTest=false;static int smokeResult=1;
static NSString* ns(const std::string& s){return [NSString stringWithUTF8String:s.c_str()]?:@"Invalid text";}
static std::filesystem::path settingsPath(){if(smoke)return std::filesystem::temp_directory_path()/"url2keyfill-rack-smoke"/"session.json";NSString* p=NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory,NSUserDomainMask,YES).firstObject;return std::filesystem::path(p.UTF8String)/"URL2KeyFill"/"session.json";}
struct Runtime {std::string id;std::shared_ptr<LatestFrame> frame=std::make_shared<LatestFrame>();std::shared_ptr<AudioLane> audio=std::make_shared<AudioLane>();CefRefPtr<Browser> browser;bool closed=false;};
@interface Preview:NSView { @public std::shared_ptr<const Frame> frame; BOOL key; }
@end
@implementation Preview
-(BOOL)isFlipped{return YES;}
-(void)drawRect:(NSRect)dirty{
 for(int y=0;y<self.bounds.size.height;y+=20)for(int x=0;x<self.bounds.size.width;x+=20){[((x/20+y/20)%2?[NSColor colorWithWhite:.24 alpha:1]:[NSColor colorWithWhite:.17 alpha:1]) setFill];NSRectFill(NSMakeRect(x,y,20,20));}
 if(!frame||self.bounds.size.width<1||self.bounds.size.height<1)return;
 const auto& f=*frame;auto data=f.pixels;
 if(key)for(size_t i=0;i<data.size();i+=4){data[i]=data[i+1]=data[i+2]=data[i+3];data[i+3]=255;}
 CFDataRef bytes=CFDataCreate(nullptr,data.data(),data.size());CGDataProviderRef provider=CGDataProviderCreateWithCFData(bytes);
 CGColorSpaceRef color=CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
 CGImageRef image=CGImageCreate(f.size.width,f.size.height,8,32,f.stride,color,CGBitmapInfo(kCGBitmapByteOrder32Little)|CGBitmapInfo(kCGImageAlphaPremultipliedFirst),provider,nullptr,false,kCGRenderingIntentDefault);
 auto r=fit(f.size,{int(self.bounds.size.width),int(self.bounds.size.height)});
 CGContextRef context=NSGraphicsContext.currentContext.CGContext;CGContextSaveGState(context);
 CGContextTranslateCTM(context,r.x,r.y+r.height);CGContextScaleCTM(context,1,-1);CGContextDrawImage(context,CGRectMake(0,0,r.width,r.height),image);CGContextRestoreGState(context);
 CGImageRelease(image);CGColorSpaceRelease(color);CGDataProviderRelease(provider);CFRelease(bytes);
}
@end
@interface RackWindow:NSWindow
@end
@implementation RackWindow
-(BOOL)canBecomeKeyWindow{return YES;}
-(BOOL)canBecomeMainWindow{return YES;}
@end
@interface AppDelegate:NSObject<NSApplicationDelegate,NSWindowDelegate,WKScriptMessageHandler,WKNavigationDelegate,WKUIDelegate>{
 RackWindow* window;WKWebView* web;Preview* preview;NSTimer* timer;NSMutableDictionary* prefs;NSMutableArray<NSMutableDictionary*>* layers;
 std::unique_ptr<Scene> scene;std::shared_ptr<AudioMixer> mixer;std::unique_ptr<Output> output;
 std::vector<Device> devices;std::unordered_map<std::string,std::shared_ptr<Runtime>> runtimes;std::vector<std::shared_ptr<Runtime>> retired;
 BOOL closing,ready,openSetup,dragging,wasRunning;NSPoint dragScreen,dragOrigin;NSString* message;int tick,smokePhase,uiSmoke;uint64_t lastSequence,lastComposed,lastCompleted;double browserRate,composeRate,sdIRate;std::chrono::steady_clock::time_point measured;std::chrono::steady_clock::time_point started;
}
-(void)setup;
-(void)quit;
-(void)requestQuit;
@end
@interface U2KApplication:NSApplication<CefAppProtocol>{BOOL sending;}
@end
@implementation U2KApplication
-(BOOL)isHandlingSendEvent{return sending;}
-(void)setHandlingSendEvent:(BOOL)b{sending=b;}
-(void)sendEvent:(NSEvent*)e{CefScopedSendingEvent scope;[super sendEvent:e];}
-(void)terminate:(id)sender{[(AppDelegate*)self.delegate requestQuit];}
@end
@implementation AppDelegate
-(NSMutableDictionary*)newLayer{
 return [@{@"id":NSUUID.UUID.UUIDString,@"name":@"Test layer",@"url":@"u2k://test/index.html",@"visible":@YES,@"muted":@NO,@"gain":@1} mutableCopy];
}
-(void)save{
 if(closing)return;
 NSDictionary* state=@{@"version":@1,@"preferences":prefs,@"layers":layers};NSError* error=nil;NSData* data=[NSJSONSerialization dataWithJSONObject:state options:NSJSONWritingPrettyPrinted error:&error];if(!data)throw std::runtime_error(error.localizedDescription.UTF8String);
 auto p=settingsPath();std::filesystem::create_directories(p.parent_path());if(![data writeToFile:ns(p.string()) options:NSDataWritingAtomic error:&error])throw std::runtime_error(error.localizedDescription.UTF8String);
}
-(u2k::Size)viewport{
 NSString* preset=prefs[@"browserSize"];if([preset isEqual:@"match"]){if(auto m=[self selectedMode])return m->size;return {1920,1080};}
 if([preset isEqual:@"1920x1080"])return {1920,1080};if([preset isEqual:@"3840x2160"])return {3840,2160};return {[prefs[@"width"] intValue],[prefs[@"height"] intValue]};
}
-(Mode*)selectedMode{
 for(auto& d:devices)if(d.id==[prefs[@"device"] UTF8String])for(auto& m:d.modes)if(m.id==[prefs[@"mode"] unsignedIntValue]&&((m.pixelFormat==0x42475241)==[prefs[@"pixel"] isEqual:@"BGRA"]))return &m;return nullptr;
}
-(void)chooseMode{
 Device* device=nullptr;for(auto& d:devices)if(d.id==[prefs[@"device"] UTF8String])device=&d;if(!device)return;
 Mode* found=nullptr;for(auto& m:device->modes)if(m.id==[prefs[@"mode"] unsignedIntValue]&&((m.pixelFormat==0x42475241)==[prefs[@"pixel"] isEqual:@"BGRA"]))found=&m;
 if(!found){for(auto& m:device->modes)if(m.id==[prefs[@"mode"] unsignedIntValue]){found=&m;break;}}
 if(!found&&!device->modes.empty())found=&device->modes.front();if(!found)return;
 prefs[@"mode"]=@(found->id);prefs[@"pixel"]=found->pixelFormat==0x42475241?@"BGRA":@"ARGB";
 if(!found->keying)prefs[@"keying"]=@NO;else if(!found->fillOnlySupported)prefs[@"keying"]=@YES;
}
-(void)refresh{
 if(output->status().running)throw std::runtime_error("Stop PLAY before refreshing devices.");
 devices=noDeviceTest?std::vector<Device>{}:output->enumerate();
 if([prefs[@"preferredDevice"] length]==0&&[prefs[@"device"] length])prefs[@"preferredDevice"]=prefs[@"device"];
 bool preferred=false;for(auto& d:devices)if(d.id==[prefs[@"preferredDevice"] UTF8String])preferred=true;
 prefs[@"device"]=preferred?prefs[@"preferredDevice"]:(devices.empty()?@"":ns(devices.front().id));
 if([prefs[@"preferredDevice"] length]==0&&[prefs[@"device"] length])prefs[@"preferredDevice"]=prefs[@"device"];
 [self chooseMode];openSetup=devices.empty();[self save];message=ns(output->status().message);
}
-(void)configureScene{
 u2k::Size size=[self viewport];validate(size);u2k::Size target=size;if(auto m=[self selectedMode])target=m->size;
 std::vector<ImageInput> images;std::vector<AudioInput> audio;
 for(NSDictionary* layer in layers){auto it=runtimes.find([layer[@"id"] UTF8String]);if(it==runtimes.end())continue;images.push_back({it->second->frame,[layer[@"visible"] boolValue]});audio.push_back({it->second->audio,[layer[@"visible"] boolValue]&&![layer[@"muted"] boolValue]&&![prefs[@"audioMuted"] boolValue],[layer[@"gain"] floatValue]});}
 scene->configure(std::move(images),size,target);mixer->inputs(std::move(audio));
}
-(void)createRuntime:(NSDictionary*)layer{
 auto r=std::make_shared<Runtime>();r->id=[layer[@"id"] UTF8String];r->browser=new Browser(*r->frame,[self viewport],r->audio);std::weak_ptr<Runtime> weak=r;r->browser->closed=[weak]{if(auto r=weak.lock())r->closed=true;};runtimes.emplace(r->id,r);r->browser->create([layer[@"url"] UTF8String]);
}
-(void)reloadLayer:(NSDictionary*)layer{
 auto it=runtimes.find([layer[@"id"] UTF8String]);if(it!=runtimes.end()){it->second->audio->clear();it->second->browser->load([layer[@"url"] UTF8String],[self viewport]);}
}
-(void)setup{
 scene=std::make_unique<Scene>();mixer=std::make_shared<AudioMixer>();output=makeOutput();output->audio(mixer);message=@"Ready";
 prefs=[@{@"device":@"",@"preferredDevice":@"",@"mode":@0,@"pixel":@"BGRA",@"keying":@YES,@"browserSize":@"match",@"width":@1920,@"height":@1080,@"layersVisible":@YES,@"audioMuted":@NO} mutableCopy];layers=[NSMutableArray array];
 BOOL hasSession=NO;
 if(!smoke){NSData* data=[NSData dataWithContentsOfFile:ns(settingsPath().string())];if(data){NSError* error=nil;id saved=[NSJSONSerialization JSONObjectWithData:data options:NSJSONReadingMutableContainers error:&error];if([saved isKindOfClass:NSDictionary.class]&&[saved[@"preferences"] isKindOfClass:NSDictionary.class]&&[saved[@"layers"] isKindOfClass:NSArray.class]){hasSession=YES;for(NSString* key in prefs.allKeys){id value=saved[@"preferences"][key];if([value isKindOfClass:[prefs[key] class]]||([prefs[key] isKindOfClass:NSNumber.class]&&[value isKindOfClass:NSNumber.class])||([prefs[key] isKindOfClass:NSString.class]&&[value isKindOfClass:NSString.class]))prefs[key]=value;}for(id l in saved[@"layers"]){if(![l isKindOfClass:NSDictionary.class])continue;NSMutableDictionary* v=[[self newLayer] mutableCopy];[v addEntriesFromDictionary:l];if([v[@"id"] isKindOfClass:NSString.class]&&[v[@"name"] isKindOfClass:NSString.class]&&[v[@"url"] isKindOfClass:NSString.class]&&allowedUrl([v[@"url"] UTF8String]))[layers addObject:v];}}else{auto backup=settingsPath();backup+=".invalid-backup";std::filesystem::copy_file(settingsPath(),backup,std::filesystem::copy_options::overwrite_existing);message=@"Invalid configuration preserved in session.json.invalid-backup";}}}
 if(!hasSession){NSMutableDictionary* first=[self newLayer];if(!smoke){auto old=settingsPath().parent_path()/"settings.txt";try{if(std::filesystem::exists(old)){auto legacy=Settings::load(old);first[@"url"]=ns(legacy.url);prefs[@"device"]=ns(legacy.device);prefs[@"mode"]=@(legacy.mode);prefs[@"browserSize"]=legacy.matchOutput?@"match":@"custom";prefs[@"width"]=@(legacy.viewport.width);prefs[@"height"]=@(legacy.viewport.height);}}catch(const std::exception& e){message=ns(e.what());}}[layers addObject:first];}
 if(![@[@"match",@"1920x1080",@"3840x2160",@"custom"] containsObject:prefs[@"browserSize"]])prefs[@"browserSize"]=@"match";
 if(![@[@"BGRA",@"ARGB"] containsObject:prefs[@"pixel"]])prefs[@"pixel"]=@"BGRA";
 NSMutableSet* identities=[NSMutableSet set];for(NSMutableDictionary* layer in [layers copy]){if([identities containsObject:layer[@"id"]]||[layer[@"id"] length]<1||[layer[@"id"] length]>80||[layer[@"name"] length]<1||[layer[@"name"] length]>80||identities.count>=16){[layers removeObjectIdenticalTo:layer];continue;}[identities addObject:layer[@"id"]];for(NSString* key in @[@"visible",@"muted"])if(![layer[key] isKindOfClass:NSNumber.class])layer[key]=@NO;if(![layer[@"gain"] isKindOfClass:NSNumber.class]||!std::isfinite([layer[@"gain"] doubleValue])||[layer[@"gain"] doubleValue]<0||[layer[@"gain"] doubleValue]>2)layer[@"gain"]=@1;}
 try{validate([self viewport]);}catch(...){prefs[@"browserSize"]=@"match";}
 if(![prefs[@"device"] isKindOfClass:NSString.class])prefs[@"device"]=@"";
 window=[[RackWindow alloc]initWithContentRect:NSMakeRect(0,0,1280,460) styleMask:NSWindowStyleMaskBorderless|NSWindowStyleMaskResizable backing:NSBackingStoreBuffered defer:NO];window.title=@"URL2KeyFill";window.delegate=self;window.minSize=NSMakeSize(1050,390);window.opaque=NO;window.backgroundColor=NSColor.clearColor;window.contentView.wantsLayer=YES;window.contentView.layer.cornerRadius=13;window.contentView.layer.masksToBounds=YES;[window center];
 WKWebViewConfiguration* configuration=[[WKWebViewConfiguration alloc]init];configuration.websiteDataStore=WKWebsiteDataStore.nonPersistentDataStore;[configuration.userContentController addScriptMessageHandler:self name:@"native"];web=[[WKWebView alloc]initWithFrame:window.contentView.bounds configuration:configuration];web.autoresizingMask=NSViewWidthSizable|NSViewHeightSizable;web.navigationDelegate=self;web.UIDelegate=self;[web setValue:@NO forKey:@"drawsBackground"];[window.contentView addSubview:web];
 preview=[[Preview alloc]initWithFrame:NSZeroRect];[window.contentView addSubview:preview];
 NSMenu* menu=[[NSMenu alloc]init];NSMenuItem* appItem=[[NSMenuItem alloc]init];NSMenu* appMenu=[[NSMenu alloc]init];[appMenu addItemWithTitle:@"Quit URL2KeyFill" action:@selector(terminate:) keyEquivalent:@"q"];appItem.submenu=appMenu;[menu addItem:appItem];NSMenuItem* editItem=[[NSMenuItem alloc]init];NSMenu* edit=[[NSMenu alloc]initWithTitle:@"Edit"];for(NSArray* e in @[@[@"Cut",@"cut:",@"x"],@[@"Copy",@"copy:",@"c"],@[@"Paste",@"paste:",@"v"],@[@"Select All",@"selectAll:",@"a"]])[edit addItemWithTitle:e[0] action:NSSelectorFromString(e[1]) keyEquivalent:e[2]];editItem.submenu=edit;[menu addItem:editItem];NSApp.mainMenu=menu;
 std::ifstream html(std::string(NSBundle.mainBundle.resourcePath.UTF8String)+"/test.html");std::stringstream content;content<<html.rdbuf();if(smoke)content<<R"(<script>const a=new AudioContext({sampleRate:48000});const o=a.createOscillator(),g=a.createGain();o.frequency.value=440;g.gain.value=.1;o.connect(g);g.connect(a.destination);o.start();a.resume();</script>)";registerTestPage(content.str());
 [self refresh];if(smoke){prefs[@"mode"]=@(1215312437);prefs[@"browserSize"]=@"1920x1080";[self chooseMode];}for(NSDictionary* layer in layers)[self createRuntime:layer];[self configureScene];
 NSURL* ui=[NSBundle.mainBundle URLForResource:@"interface" withExtension:@"html"];[web loadFileURL:ui allowingReadAccessToURL:ui];
 timer=[NSTimer scheduledTimerWithTimeInterval:.05 target:self selector:@selector(update:) userInfo:nil repeats:YES];started=std::chrono::steady_clock::now();measured=started;[window makeKeyAndOrderFront:nil];[NSApp activateIgnoringOtherApps:YES];
}
-(void)webView:(WKWebView*)view decidePolicyForNavigationAction:(WKNavigationAction*)action decisionHandler:(void (^)(WKNavigationActionPolicy))decision{
 NSURL* url=action.request.URL;if(url.isFileURL&&[url.lastPathComponent isEqual:@"interface.html"]){decision(WKNavigationActionPolicyAllow);return;}
 if(action.navigationType==WKNavigationTypeLinkActivated&&[url.host isEqual:@"www.blackmagicdesign.com"]&&[url.scheme isEqual:@"https"])[NSWorkspace.sharedWorkspace openURL:url];decision(WKNavigationActionPolicyCancel);
}
-(WKWebView*)webView:(WKWebView*)view createWebViewWithConfiguration:(WKWebViewConfiguration*)configuration forNavigationAction:(WKNavigationAction*)action windowFeatures:(WKWindowFeatures*)features{NSURL* url=action.request.URL;if([url.scheme isEqual:@"https"]&&[url.host isEqual:@"www.blackmagicdesign.com"])[NSWorkspace.sharedWorkspace openURL:url];return nil;}
-(void)userContentController:(WKUserContentController*)controller didReceiveScriptMessage:(WKScriptMessage*)msg{
 if(!msg.frameInfo.isMainFrame||![msg.frameInfo.request.URL.lastPathComponent isEqual:@"interface.html"]||![msg.body isKindOfClass:NSDictionary.class])return;
 NSDictionary* data=msg.body;NSString* type=data[@"type"];if(![type isKindOfClass:NSString.class])return;
 try{
 if([type isEqual:@"ready"]){ready=YES;[self sendState];return;}
 if([type isEqual:@"layout"]){double h=[data[@"heightWindow"] doubleValue];if(std::isfinite(h)&&h>=300&&h<2000){NSRect f=window.frame;double desired=std::min(h,NSScreen.mainScreen.visibleFrame.size.height-30);if(std::abs(f.size.height-desired)>2){f.origin.y+=f.size.height-desired;f.size.height=desired;[window setFrame:f display:YES];}}
  NSRect r=NSMakeRect([data[@"x"] doubleValue],window.contentView.bounds.size.height-[data[@"y"] doubleValue]-[data[@"height"] doubleValue],[data[@"width"] doubleValue],[data[@"height"] doubleValue]);if(r.size.width>=0&&r.size.width<2000&&r.size.height>=0&&r.size.height<2000)preview.frame=r;preview.hidden=![data[@"show"] boolValue];return;}
 if([type isEqual:@"drag"]){dragging=YES;dragScreen=NSEvent.mouseLocation;dragOrigin=window.frame.origin;return;}
 if([type isEqual:@"requestQuit"]){[self requestQuit];return;}
 if([type isEqual:@"quit"]){[self quit];return;}
 if([type isEqual:@"monitor"]){preview->key=[data[@"key"] boolValue];[preview setNeedsDisplay:YES];return;}
 if([type isEqual:@"refresh"]){[self refresh];}
 else if([type isEqual:@"play"]){if(output->status().running){output->stop();wasRunning=NO;message=@"SDI output stopped";}else{auto m=[self selectedMode];if(!m)throw std::runtime_error("No compatible output mode. Open SETUP.");Mode requested=*m;requested.keying=[prefs[@"keying"] boolValue];output->start([prefs[@"device"] UTF8String],requested,scene->frames);wasRunning=YES;message=@"SDI output started";}}
 else if([type isEqual:@"pause"]){if(output->status().running){bool air=!output->status().onAir;output->onAir(air);message=air?@"Image and audio restored":@"Paused · Image transparent, audio silent, SDI active";}}
 else if([type isEqual:@"layersVisible"]){prefs[@"layersVisible"]=@([data[@"value"] boolValue]);}
 else if([type isEqual:@"add"]){if(layers.count>=16)throw std::runtime_error("This development build supports up to 16 layers. Measure resource use before adding more.");NSMutableDictionary* l=[self newLayer];l[@"name"]=@"New layer";l[@"visible"]=@NO;[layers insertObject:l atIndex:0];[self createRuntime:l];message=@"Test layer added at top";}
 else if([type isEqual:@"reload"]){for(NSDictionary* layer in layers)if(!data[@"id"]||[layer[@"id"] isEqual:data[@"id"]])[self reloadLayer:layer];message=data[@"id"]?@"Layer reloaded":@"All layers reloaded";}
 else if([type isEqual:@"delete"]){for(NSUInteger i=0;i<layers.count;i++)if([layers[i][@"id"] isEqual:data[@"id"]]){auto it=runtimes.find([data[@"id"] UTF8String]);if(it!=runtimes.end()){it->second->browser->close();retired.push_back(it->second);runtimes.erase(it);}[layers removeObjectAtIndex:i];message=@"Layer deleted";break;}}
 else if([type isEqual:@"layer"]){for(NSMutableDictionary* layer in layers)if([layer[@"id"] isEqual:data[@"id"]]){
  if(data[@"name"]){if(![data[@"name"] isKindOfClass:NSString.class]||[data[@"name"] length]<1||[data[@"name"] length]>80)throw std::runtime_error("Layer name must contain 1–80 characters.");layer[@"name"]=data[@"name"];}
  if(data[@"url"]){if(![data[@"url"] isKindOfClass:NSString.class]||!allowedUrl([data[@"url"] UTF8String]))throw std::runtime_error("Use an HTTP/HTTPS URL or the built-in test page.");if(![layer[@"url"] isEqual:data[@"url"]]){layer[@"url"]=data[@"url"];[self reloadLayer:layer];message=@"Layer URL updated and reloaded";}}
  for(NSString* key in @[@"visible",@"muted"])if(data[key])layer[key]=@([data[key] boolValue]);if(data[@"gain"]){double gain=[data[@"gain"] doubleValue];if(!std::isfinite(gain)||gain<0||gain>2)throw std::runtime_error("Gain must be between 0 and 2.");layer[@"gain"]=@(gain);}break;}}
 else if([type isEqual:@"reorder"]){NSArray* ids=data[@"ids"];if(![ids isKindOfClass:NSArray.class]||ids.count!=layers.count)throw std::runtime_error("Invalid layer order.");NSMutableArray* ordered=[NSMutableArray array];for(id identity in ids){for(NSDictionary* l in layers)if([l[@"id"] isEqual:identity]&&![ordered containsObject:l])[ordered addObject:l];}if(ordered.count!=layers.count)throw std::runtime_error("Invalid layer order.");layers=ordered;message=@"Layer order updated";}
 else if([type isEqual:@"configure"]){if(output->status().running)throw std::runtime_error("Stop PLAY before changing output settings.");NSMutableDictionary* next=[prefs mutableCopy];NSString* device=data[@"device"];bool valid=false;for(auto& d:devices)if(d.id==[device UTF8String])valid=true;if(!valid)throw std::runtime_error("Select a detected device.");next[@"device"]=device;next[@"preferredDevice"]=device;next[@"mode"]=@([data[@"mode"] unsignedIntValue]);if(![@[@"BGRA",@"ARGB"] containsObject:data[@"pixel"]])throw std::runtime_error("Unsupported pixel format.");next[@"pixel"]=data[@"pixel"];next[@"keying"]=@([data[@"keying"] boolValue]);if(![@[@"match",@"1920x1080",@"3840x2160",@"custom"] containsObject:data[@"browserSize"]])throw std::runtime_error("Unsupported browser size.");next[@"browserSize"]=data[@"browserSize"];next[@"width"]=@([data[@"width"] intValue]);next[@"height"]=@([data[@"height"] intValue]);validate({[next[@"width"] intValue],[next[@"height"] intValue]});next[@"audioMuted"]=@([data[@"audioMuted"] boolValue]);u2k::Size old=[self viewport];BOOL deviceChanged=![next[@"device"] isEqual:prefs[@"device"]];prefs=next;[self chooseMode];if(deviceChanged)if(auto m=[self selectedMode])prefs[@"keying"]=@(m->keying);if(!(old==[self viewport]))for(NSDictionary* layer in layers)[self reloadLayer:layer];message=@"Configuration saved";}
 [self configureScene];[self save];[self sendState];
 }catch(const std::exception& e){message=ns(e.what());[self sendState];}
}
-(void)sendState{
 if(!ready||closing)return;
 NSMutableArray* deviceList=[NSMutableArray array];for(auto& d:devices)[deviceList addObject:@{@"id":ns(d.id),@"name":ns(d.name)}];NSMutableArray* modeList=[NSMutableArray array],*pixels=[NSMutableArray array],*outputs=[NSMutableArray array];std::vector<uint32_t> seen;
 for(auto& d:devices)if(d.id==[prefs[@"device"] UTF8String])for(auto& m:d.modes){if(std::find(seen.begin(),seen.end(),m.id)==seen.end()){seen.push_back(m.id);[modeList addObject:@{@"id":@(m.id),@"name":ns(m.name.substr(0,m.name.find(" · ")))}];}if(m.id==[prefs[@"mode"] unsignedIntValue]){NSString* p=m.pixelFormat==0x42475241?@"BGRA":@"ARGB";if(![pixels containsObject:p])[pixels addObject:p];}}
 Mode* mode=[self selectedMode];if(mode){if(mode->keying)[outputs addObject:@"Fill + Key"];if(mode->fillOnlySupported)[outputs addObject:@"Fill only"];}
 auto status=output->status();u2k::Size size=[self viewport];NSString* deviceName=@"";for(auto& d:devices)if(d.id==[prefs[@"device"] UTF8String])deviceName=ns(d.name);
 uint64_t captured=0,discarded=0;NSMutableArray* pageStates=[NSMutableArray array];for(auto& entry:runtimes){captured+=entry.second->audio->received;discarded+=entry.second->audio->discarded;[pageStates addObject:ns(entry.second->browser->message)];}
 std::ostringstream metrics;metrics<<std::fixed<<std::setprecision(1)<<"CEF "<<browserRate<<" fps · Composite "<<composeRate<<" fps · SDI "<<sdIRate<<" fps · Browser "<<size.width<<" × "<<size.height<<" · ";if(mode)metrics<<"SDI "<<mode->rate.label()<<" · ";metrics<<"Composites "<<scene->composed<<" · SDI frames "<<status.completed<<" · Late "<<status.late<<" · Dropped "<<status.dropped<<" · Audio captured "<<captured<<" / SDI "<<status.audioFrames<<" frames · Audio errors "<<status.audioErrors<<" · Audio packets discarded "<<discarded<<" · Audible audio samples "<<mixer->nonSilent<<" · Limited samples "<<mixer->clipped;
 NSString* detail=[pageStates componentsJoinedByString:@" | "];NSString* diagnostics=[ns(metrics.str()) stringByAppendingFormat:@"\n%@",detail];
 NSDictionary* state=@{@"layers":layers,@"layersVisible":prefs[@"layersVisible"],@"device":prefs[@"device"],@"deviceName":deviceName,@"devices":deviceList,@"mode":prefs[@"mode"],@"modeName":mode?ns(mode->name.substr(0,mode->name.find(" · "))):@"Unavailable",@"modes":modeList,@"pixels":pixels,@"pixel":prefs[@"pixel"],@"outputs":outputs,@"keying":prefs[@"keying"],@"width":@(size.width),@"height":@(size.height),@"browserSize":prefs[@"browserSize"],@"audioMuted":prefs[@"audioMuted"],@"output":@{@"running":@(status.running),@"onAir":@(status.onAir)},@"diagnostics":diagnostics,@"message":message?:@"Ready",@"openSetup":@(openSetup)};
 openSetup=NO;NSData* json=[NSJSONSerialization dataWithJSONObject:state options:0 error:nil];NSString* script=[@"window.receiveState(" stringByAppendingFormat:@"%@);",[[NSString alloc]initWithData:json encoding:NSUTF8StringEncoding]];[web evaluateJavaScript:script completionHandler:^(id value,NSError* error){if(error)std::cerr<<"UI state error: "<<error.localizedDescription.UTF8String<<'\n';}];
}
-(void)update:(NSTimer*)sender{
 if(dragging){if(!(NSEvent.pressedMouseButtons&1))dragging=NO;else{NSPoint p=NSEvent.mouseLocation;[window setFrameOrigin:NSMakePoint(dragOrigin.x+p.x-dragScreen.x,dragOrigin.y+p.y-dragScreen.y)];}}
 retired.erase(std::remove_if(retired.begin(),retired.end(),[](const auto& r){return r->closed;}),retired.end());
 if(closing){bool all=true;for(auto& r:runtimes)if(!r.second->closed)all=false;for(auto& r:retired)if(!r->closed)all=false;if(all){[timer invalidate];CefQuitMessageLoop();}return;}
 auto status=output->status();if(wasRunning&&!status.running){output->stop();wasRunning=NO;message=ns(status.message);openSetup=YES;}
 auto image=scene->frames.get();if(status.running&&!status.onAir)image=nullptr;if(image!=preview->frame){preview->frame=image;[preview setNeedsDisplay:YES];}
 auto now=std::chrono::steady_clock::now();double dt=std::chrono::duration<double>(now-measured).count();if(dt>=1){uint64_t paints=0;for(auto& r:runtimes)paints+=r.second->browser->paints();browserRate=paints>=lastSequence?(paints-lastSequence)/dt:0;composeRate=(scene->composed-lastComposed)/dt;sdIRate=status.completed>=lastCompleted?(status.completed-lastCompleted)/dt:0;lastSequence=paints;lastComposed=scene->composed;lastCompleted=status.completed;measured=now;}
 if(++tick%4==0)[self sendState];
 if(smoke){
  if(std::chrono::steady_clock::now()-started>std::chrono::seconds(25)){std::cerr<<"FAIL smoke timeout phase "<<smokePhase<<" UI="<<ready<<" video="<<status.completed<<" audio="<<status.audioFrames<<" nonSilent="<<mixer->nonSilent<<" mixPts="<<mixer->lastMixTimestamp<<'\n';for(auto& r:runtimes)std::cerr<<"Source audio frames="<<r.second->audio->received<<" pts="<<r.second->audio->latestTimestamp<<" nonzero="<<r.second->audio->nonZero<<'\n';[self quit];return;}
  auto first=runtimes.find([layers.firstObject[@"id"] UTF8String]);if(first==runtimes.end())return;auto frame=first->second->frame->get();
  if(smokePhase==0&&frame&&ready){
   if(uiSmoke<2){if(uiSmoke==0){uiSmoke=1;[web evaluateJavaScript:ns(R"JS((()=>{const q=s=>document.querySelector(s),assert=(x,m)=>{if(!x)throw Error(m)};if(!q('.sr-device-select').options.length){assert(!q('.sr-driver-help').hidden,'Driver help missing');assert(q('[data-action=play]').disabled,'Play enabled without hardware');}const r=q('.sr-picture').getBoundingClientRect();q('.sr-chassis').classList.add('layers-hidden');const hidden=q('.sr-picture').getBoundingClientRect();assert(Math.abs(r.width-hidden.width)<1&&Math.abs(r.height-hidden.height)<1,'Monitor changes size');q('.sr-chassis').classList.remove('layers-hidden');q('[data-action="edit"]').click();q('.sr-layer-toggle').click();assert(q('.sr-layer-drawer').classList.contains('open'),'Layer drawer did not open');q('.sr-layer-toggle').click();assert(!q('.sr-layer-drawer').classList.contains('open'),'Layer drawer did not toggle closed');q('.sr-layer-toggle').click();q('[data-action="setup"]').click();assert(!q('.sr-layer-drawer').classList.contains('open')&&q('.sr-setup-drawer').classList.contains('open'),'Drawers not exclusive');const size=q('.sr-browser-size');size.value='custom';size.dispatchEvent(new Event('change'));assert(!q('.sr-custom-size').hidden,'Custom dimensions missing');size.value='1920x1080';size.dispatchEvent(new Event('change'));q('[data-action="close-setup"]').click();q('[data-action="edit"]').click();assert(q('[data-action="pause"]').disabled,'Pause allowed without Play');assert(q('.sr-matrix').children.length===8,'Wrong button matrix');return true;})())JS") completionHandler:^(id result,NSError* error){if(error||![result boolValue]){std::cerr<<"FAIL UI behavior: "<<(error?error.localizedDescription.UTF8String:"assertion")<<'\n';[self quit];}else{uiSmoke=2;std::cout<<"PASS monitor size, drawers, edit toggle, custom dimensions and pause guard\n";}}];}return;}
auto alpha=frame->pixels[80*frame->stride+110*4+3];if(alpha<120||alpha>135){std::cerr<<"FAIL browser alpha\n";[self quit];return;}std::cout<<"PASS native rack UI bridge and real transparent CEF rendering\n";auto l=[self newLayer];l[@"name"]=@"Second test";[layers insertObject:l atIndex:0];[self createRuntime:l];[self configureScene];smokePhase=1;}
  else if(smokePhase==1&&image&&image->size==u2k::Size{1920,1080}&&image->pixels[80*image->stride+110*4+3]>185){std::cout<<"PASS independent browser layers and premultiplied alpha composition\n";layers[0][@"visible"]=@NO;[self configureScene];smokePhase=2;}
  else if(smokePhase==2&&image&&image->pixels[80*image->stride+110*4+3]>=120&&image->pixels[80*image->stride+110*4+3]<=135){std::cout<<"PASS visibility changes composite without unloading browser\n";[self save];NSData* data=[NSData dataWithContentsOfFile:ns(settingsPath().string())];NSDictionary* saved=[NSJSONSerialization JSONObjectWithData:data options:0 error:nil];if([saved[@"layers"] count]!=2){[self quit];return;}std::cout<<"PASS persisted layer configuration\n";smokePhase=3;}
  else if(smokePhase==3){uint64_t audio=0;for(auto& r:runtimes)audio+=r.second->audio->received;if(audio>4800){std::cout<<"PASS CEF 48 kHz stereo audio capture: "<<audio<<" samples\n";if(hardwareTest){auto m=[self selectedMode];if(!m){std::cerr<<"FAIL no hardware for SDI test\n";[self quit];return;}output->start([prefs[@"device"] UTF8String],*m,scene->frames);wasRunning=YES;smokePhase=4;}else{smokeResult=0;[self quit];}}}
  else if(smokePhase==4&&status.completed>120&&status.audioFrames>48000&&mixer->nonSilent>4800){output->onAir(false);smokePhase=5;std::cout<<"PASS scheduled SDI video and stereo audio\n";}
  else if(smokePhase==5&&status.completed>150){output->onAir(true);output->stop();smokeResult=0;std::cout<<"PASS SDI pause, resume and stop\n";[self quit];}
 }
}
-(void)requestQuit{if(!output||!output->status().running){[self quit];return;}if(ready)[web evaluateJavaScript:@"window.confirmQuit()" completionHandler:nil];else{NSAlert* alert=[[NSAlert alloc]init];alert.alertStyle=NSAlertStyleCritical;alert.messageText=@"WARNING — SDI OUTPUT ACTIVE";alert.informativeText=@"Quitting will immediately cut the SDI video and audio signal. This may interrupt a live broadcast. Are you sure you want to stop output and quit?";[alert addButtonWithTitle:@"Cancel"];[alert addButtonWithTitle:@"Stop output and quit"];if([alert runModal]==NSAlertSecondButtonReturn)[self quit];}}
-(BOOL)windowShouldClose:(NSWindow*)sender{[self requestQuit];return NO;}
-(void)quit{if(closing)return;try{[self save];}catch(const std::exception& e){std::cerr<<e.what()<<'\n';}closing=YES;output->stop();[web.configuration.userContentController removeScriptMessageHandlerForName:@"native"];for(auto& r:runtimes)r.second->browser->close();for(auto& r:retired)r->browser->close();}
-(BOOL)applicationSupportsSecureRestorableState:(NSApplication*)app{return YES;}
@end
int main(int argc,char** argv){
 for(int i=1;i<argc;i++){if(std::string(argv[i])=="--smoke-test")smoke=true;if(std::string(argv[i])=="--smoke-no-device-test"){smoke=true;noDeviceTest=true;}if(std::string(argv[i])=="--hardware-smoke-test"){smoke=true;hardwareTest=true;}}
 CefScopedLibraryLoader loader;if(!loader.LoadInMain())return 1;
 @autoreleasepool{[U2KApplication sharedApplication];[NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];AppDelegate* delegate=[[AppDelegate alloc]init];NSApp.delegate=delegate;CefRefPtr<BrowserApp> app=new BrowserApp;app->ready=[delegate]{[delegate setup];};
 CefSettings config;config.windowless_rendering_enabled=true;config.no_sandbox=false;config.command_line_args_disabled=true;auto cache=std::filesystem::temp_directory_path()/("url2keyfill-cef-"+std::string(NSUUID.UUID.UUIDString.UTF8String));CefString(&config.root_cache_path)=cache.string();CefString(&config.log_file)=(cache/"cef.log").string();CefMainArgs args(argc,argv);if(!CefInitialize(args,config,app,nullptr))return CefGetExitCode();
 CefRunMessageLoop();app->ready=nullptr;NSApp.delegate=nil;delegate=nil;app=nullptr;CefShutdown();std::error_code error;std::filesystem::remove_all(cache,error);}
 if(smoke)std::cout<<"CEF shutdown complete\n";return smoke?smokeResult:0;
}
