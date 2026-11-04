#import <Cocoa/Cocoa.h>
#import <WebKit/WebKit.h>
#import <QuartzCore/QuartzCore.h>
#import <AVFoundation/AVFoundation.h>
#include "browser/Browser.hpp"
#include "output/Output.hpp"
#include "core/AVAdjustment.hpp"
#include "core/AVCalibration.hpp"
#include "core/MediaClock.hpp"
#include "include/cef_application_mac.h"
#include "include/cef_version.h"
#include "include/wrapper/cef_helpers.h"
#include "include/wrapper/cef_library_loader.h"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <iostream>
#include <unordered_map>
#include <cmath>
#include <algorithm>
#include <thread>
using namespace u2k;
// Marketing version and build from Info.plist (CMake project version), as the About panel shows them.
static NSString* appVersion(){NSDictionary* info=NSBundle.mainBundle.infoDictionary;return [NSString stringWithFormat:@"%@ (%@)",info[@"CFBundleShortVersionString"]?:@"?",info[@"CFBundleVersion"]?:@"?"];}
static std::string diagnosticStopFile;
static std::string syncCalibrationFile;static int syncCalibrationLast=INT_MAX;
static std::string requestedTimingProfile;static int requestedSyncOffset=INT_MAX,changedSyncOffset=INT_MAX;static double syncChangeSeconds=15;static bool syncChanged=false;static bool explicitSourceAge=false;
static bool rawAV=false; // Measurement runs: no calibrated default.
static int64_t cefRateOverride=0; // Experiment: force N x the SDI cadence for CEF paint (0 = rule).
static bool externalBeginFrame=false,beginFrameProbe=false;
// Prototype render clock (audit §11.2 item 3). Chromium paints once per BeginFrame, sent at the
// host instant each SDI frame selects its source, or at the nominal mode rate while SDI is idle.
struct RenderClock {std::atomic<bool> quit{false};std::atomic<double> period{800.8};std::atomic<int64_t> subdivisions{1};std::thread thread;std::atomic<uint64_t> sent{0},locked{0},lateDispatch{0};std::atomic<int64_t> maxDispatch{INT64_MIN},minDispatch{INT64_MAX},sumDispatch{0};
 // Main-queue latency is measured and fired ahead of, so the BeginFrame stamp lands on the grid.
 std::atomic<int64_t> lead{48};double latency=48;};
static NSString* avLayerId; // The A/V source layer the loopback test watches.
// Overlay: a second, animated layer above the A/V source.
static std::string avTestUrl,avOverlayUrl;static bool referenceIds=false,referenceContentPts=false;
static bool smoke=false,hardwareTest=false,noDeviceTest=false,monitorTest=false,avLoopbackTest=false;static int smokeResult=1;static double baselineSeconds=0;static uint32_t sourceAgeSamples=2880;static uint32_t hardwareTestMode=0x48703235;
static NSString* ns(const std::string& s){return [NSString stringWithUTF8String:s.c_str()]?:@"Invalid text";}
static std::filesystem::path settingsPath(){if(smoke)return std::filesystem::temp_directory_path()/"url2keyfill-rack-smoke"/"session.json";NSString* p=NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory,NSUserDomainMask,YES).firstObject;return std::filesystem::path(p.UTF8String)/"URL2KeyFill"/"session.json";}
struct Runtime {std::string id;std::shared_ptr<LatestFrame> frame=std::make_shared<LatestFrame>();std::shared_ptr<AudioLane> audio=std::make_shared<AudioLane>();CefRefPtr<Browser> browser;bool closed=false;};
@interface Preview:NSView { @public std::shared_ptr<const Frame> frame; BOOL key; }
@end
@implementation Preview
-(BOOL)isFlipped{return YES;}
-(void)drawRect:(NSRect)dirty{
 // Edge tiles can extend beyond the preview; keep all drawing inside the screen.
 NSRectClip(self.bounds);
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
// Right-click shows only text editing: Cut/Copy/Paste/Select All in a field, Copy on
// selected text. The page blocks the menu everywhere else. WebKit's own menu tells
// which case applies (Paste only when editable, Copy only with a selection).
@interface RackWebView:WKWebView
@end
@implementation RackWebView
-(void)willOpenMenu:(NSMenu*)menu withEvent:(NSEvent*)event{
 bool editable=false,selection=false;for(NSMenuItem* item in menu.itemArray){if([item.identifier isEqual:@"WKMenuItemIdentifierPaste"])editable=true;if([item.identifier isEqual:@"WKMenuItemIdentifierCopy"])selection=true;}
 [menu removeAllItems];menu.autoenablesItems=NO;
 const auto add=[&](NSString* title,SEL action,bool enabled){[menu addItemWithTitle:title action:action keyEquivalent:@""].enabled=enabled;};
 if(editable){add(@"Cut",@selector(cut:),selection);add(@"Copy",@selector(copy:),selection);add(@"Paste",@selector(paste:),true);[menu addItem:NSMenuItem.separatorItem];add(@"Select All",@selector(selectAll:),true);}
 else if(selection)add(@"Copy",@selector(copy:),true);
 [super willOpenMenu:menu withEvent:event];
}
@end
@interface RackWindow:NSWindow
@end
@implementation RackWindow
// There is no Edit menu and no Close item: ⌘W quits like ⌘Q, and the text-editing
// shortcuts still reach the focused field (layer name, URL, custom size).
-(BOOL)performKeyEquivalent:(NSEvent*)event{
 const NSEventModifierFlags mods=event.modifierFlags&NSEventModifierFlagDeviceIndependentFlagsMask&~(NSEventModifierFlagCapsLock|NSEventModifierFlagNumericPad|NSEventModifierFlagFunction);
 if(mods==NSEventModifierFlagCommand){NSString* key=event.charactersIgnoringModifiers.lowercaseString;
  if([key isEqual:@"w"]){[NSApp terminate:nil];return YES;}
  if([key isEqual:@"0"]&&[NSApp sendAction:NSSelectorFromString(@"showActualSize:") to:nil from:self])return YES;
  NSDictionary* edit=@{@"x":@"cut:",@"c":@"copy:",@"v":@"paste:",@"a":@"selectAll:",@"z":@"undo:"};
  if(NSString* action=edit[key])if([NSApp sendAction:NSSelectorFromString(action) to:nil from:self])return YES;}
 if(mods==(NSEventModifierFlagCommand|NSEventModifierFlagShift)&&[event.charactersIgnoringModifiers.lowercaseString isEqual:@"z"]&&[NSApp sendAction:@selector(redo:) to:nil from:self])return YES;
 return [super performKeyEquivalent:event];
}
-(BOOL)canBecomeKeyWindow{return YES;}
-(BOOL)canBecomeMainWindow{return YES;}
@end
@interface AppDelegate:NSObject<NSApplicationDelegate,NSWindowDelegate,NSMenuDelegate,WKScriptMessageHandler,WKNavigationDelegate,WKUIDelegate>{
 RackWindow* window;WKWebView* web;Preview* preview;NSTimer* timer;NSTimer* saveTimer;NSMutableDictionary* prefs;NSMutableArray<NSMutableDictionary*>* layers;NSArray<NSDictionary*>* examples;
 AVAudioEngine* audioEngine;AVAudioSourceNode* audioSource;id audioConfigurationObserver,audioWakeObserver;NSMutableSet<NSString*>* monitoredLayers;NSMutableSet<NSString*>* monitoredAFL;BOOL monitorOutput,monitorOutputPFL;std::shared_ptr<AudioMixer> monitorMixer;std::shared_ptr<std::atomic<bool>> monitorMaster,monitorProgram,monitorMasterPFL;
 std::ofstream audioTrace;std::chrono::steady_clock::time_point traceStarted;std::unordered_map<std::string,AudioReadWindow> reportedAudio;
 ClockGapDetector clockGaps;uint64_t clockResets;
 std::unique_ptr<RenderClock> renderClock;
 id<NSObject> playbackActivity;std::unique_ptr<Scene> scene;std::shared_ptr<AudioMixer> mixer;std::unique_ptr<Output> output;
 std::vector<Device> devices;std::unordered_map<std::string,std::shared_ptr<Runtime>> runtimes;std::vector<std::shared_ptr<Runtime>> retired;
 BOOL closing,ready,openSetup,dragging,wasRunning;NSPoint dragScreen,dragOrigin;NSRect fullFrame;NSString* message;NSString* openLayerId;int tick,smokePhase,uiSmoke;uint64_t lastSequence,lastComposed,lastCompleted;double browserRate,composeRate,sdIRate;std::chrono::steady_clock::time_point measured;std::chrono::steady_clock::time_point started;
}
-(void)setup;
-(void)beginFrame:(int64_t)target fired:(int64_t)fired locked:(BOOL)locked;
-(void)quit;
-(void)startSDI:(const Mode*)mode;
-(void)stopSDI;
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
 return [@{@"id":NSUUID.UUID.UUIDString,@"name":@"Test layer",@"url":@"u2k://Transparency/Basic_Test.html",@"visible":@YES,@"muted":@NO,@"mono":@NO,@"gain":@1,@"left":@YES,@"right":@YES} mutableCopy];
}
-(std::optional<int32_t>)calibratedSync{if(rawAV)return std::nullopt;if(auto m=[self selectedMode])return calibratedSyncMs(m->rate);return std::nullopt;}
// The stored value is the operator's manual adjustment; the output gets calibration + manual.
-(int32_t)effectiveSyncSamples:(int)manual{return effectiveSyncMs([self calibratedSync],manual)*48;}
-(void)startSDI:(const Mode*)mode{
 const bool low=[prefs[@"timingProfile"] isEqual:@"low"];output->timing(low?2:3,[self effectiveSyncSamples:[prefs[@"syncOffsetMs"] intValue]]);output->audioSourceAge(explicitSourceAge?sourceAgeSamples:(low?2400:2880));
 if(!playbackActivity)playbackActivity=[NSProcessInfo.processInfo beginActivityWithOptions:(NSActivityUserInitiated|NSActivityLatencyCritical) reason:@"Scheduled SDI audio and video output"];
 try{output->start([prefs[@"device"] UTF8String],*mode,scene->frames);}catch(...){if(playbackActivity){[NSProcessInfo.processInfo endActivity:playbackActivity];playbackActivity=nil;}throw;}
}
-(void)stopSDI{
 output->stop();if(playbackActivity){[NSProcessInfo.processInfo endActivity:playbackActivity];playbackActivity=nil;}
}
-(void)scheduleSave{
 [saveTimer invalidate];saveTimer=[NSTimer scheduledTimerWithTimeInterval:.5 target:self selector:@selector(flushSave:) userInfo:nil repeats:NO];
}
-(void)flushSave:(NSTimer*)sender{
 saveTimer=nil;try{[self save];}catch(const std::exception& e){message=ns(e.what());[self sendState];}
}
-(void)save{
 [saveTimer invalidate];saveTimer=nil;
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
-(void)setAudioOutputActive:(BOOL)active{
 active=active&&![prefs[@"audioMuted"] boolValue]&&mixer->gain>0;
 for(NSDictionary* layer in layers){auto runtime=runtimes.find([layer[@"id"] UTF8String]);if(runtime!=runtimes.end())runtime->second->audio->outputExpected=active&&[layer[@"visible"] boolValue]&&![layer[@"muted"] boolValue]&&[layer[@"gain"] doubleValue]>0&&audioRouteEnabled([layer[@"mono"] boolValue],[layer[@"left"] boolValue],[layer[@"right"] boolValue],mixer->mono,mixer->left,mixer->right);}
}
-(void)configureScene{
 u2k::Size size=[self viewport];validate(size);u2k::Size target=size;if(auto m=[self selectedMode])target=m->size;
 std::vector<ImageInput> images;
 for(NSDictionary* layer in layers){auto it=runtimes.find([layer[@"id"] UTF8String]);if(it==runtimes.end())continue;images.push_back({it->second->frame,[layer[@"visible"] boolValue]});}
 if(auto m=[self selectedMode])for(auto& entry:runtimes)entry.second->browser->frameRate(Rate{m->rate.duration,m->rate.scale*(cefRateOverride?cefRateOverride:cefRateMultiplier(m->rate))});
 if(renderClock)if(auto m=[self selectedMode]){const auto k=cefRateOverride?cefRateOverride:cefRateMultiplier(m->rate);renderClock->subdivisions=k;renderClock->period=48000.*double(m->rate.duration)/double(m->rate.scale*k);}
 scene->configure(std::move(images),size,target);if(auto m=[self selectedMode])scene->frames.history.retainFrames(AVTiming(m->rate).recommendedHistoryFrames(explicitSourceAge?sourceAgeSamples:2880));[self configureAudio];
}
-(void)configureAudio{
 std::vector<AudioInput> audio;
 for(NSDictionary* layer in layers){auto it=runtimes.find([layer[@"id"] UTF8String]);if(it==runtimes.end())continue;audio.push_back({it->second->audio,[layer[@"visible"] boolValue]&&![layer[@"muted"] boolValue],[layer[@"gain"] floatValue],[layer[@"mono"] boolValue],layer[@"left"]?[layer[@"left"] boolValue]:true,layer[@"right"]?[layer[@"right"] boolValue]:true});}
 mixer->inputs(std::move(audio));mixer->muted=[prefs[@"audioMuted"] boolValue];mixer->gain=prefs[@"audioGain"]?[prefs[@"audioGain"] floatValue]:1;mixer->mono=[prefs[@"audioMono"] boolValue];mixer->left=prefs[@"audioLeft"]?[prefs[@"audioLeft"] boolValue]:true;mixer->right=prefs[@"audioRight"]?[prefs[@"audioRight"] boolValue]:true;
 NSMutableSet* validMonitorIds=[NSMutableSet set];for(NSDictionary* l in layers)[validMonitorIds addObject:l[@"id"]];[monitoredLayers intersectSet:validMonitorIds];[monitoredAFL intersectSet:monitoredLayers];
 std::vector<AudioInput> listen;for(NSDictionary* layer in layers)if([monitoredLayers containsObject:layer[@"id"]]){auto it=runtimes.find([layer[@"id"] UTF8String]);if(it!=runtimes.end())listen.push_back({it->second->audio,![monitoredAFL containsObject:layer[@"id"]]||([layer[@"visible"] boolValue]&&![layer[@"muted"] boolValue]),[monitoredAFL containsObject:layer[@"id"]]?[layer[@"gain"] floatValue]:1,[layer[@"mono"] boolValue],layer[@"left"]?[layer[@"left"] boolValue]:true,layer[@"right"]?[layer[@"right"] boolValue]:true});}monitorMixer->inputs(std::move(listen));*monitorMaster=monitorOutput;*monitorMasterPFL=monitorOutputPFL;const auto status=output->status();*monitorProgram=!(status.running&&!status.onAir);[self setAudioOutputActive:status.running&&status.onAir];if(!monitorOutput&&!monitoredLayers.count)[self stopAudioMonitor];
}
-(void)createRuntime:(NSDictionary*)layer{
 auto r=std::make_shared<Runtime>();r->id=[layer[@"id"] UTF8String];r->browser=new Browser(*r->frame,[self viewport],r->audio);r->browser->externalBeginFrame=externalBeginFrame;r->browser->beginFrameProbe=beginFrameProbe;r->browser->referenceDiagnostic=referenceIds;r->browser->referenceUsePts=referenceContentPts;r->audio->referenceDiagnostic=referenceIds;r->browser->syncDiagnostic=avLoopbackTest;r->audio->syncDiagnostic=avLoopbackTest;if(auto m=[self selectedMode])r->browser->frameRate(Rate{m->rate.duration,m->rate.scale*(cefRateOverride?cefRateOverride:cefRateMultiplier(m->rate))});std::weak_ptr<Runtime> weak=r;r->browser->closed=[weak]{if(auto r=weak.lock())r->closed=true;};runtimes.emplace(r->id,r);r->browser->create([layer[@"url"] UTF8String]);
}
-(void)reloadLayer:(NSDictionary*)layer{
 auto it=runtimes.find([layer[@"id"] UTF8String]);if(it!=runtimes.end()){it->second->audio->clear();it->second->browser->load([layer[@"url"] UTF8String],[self viewport]);}
}
-(void)scanExamples{
 NSString* assets=[NSBundle.mainBundle.resourcePath stringByAppendingPathComponent:@"assets"];
 NSFileManager* manager=NSFileManager.defaultManager;NSMutableArray* catalog=[NSMutableArray array];std::map<std::string,std::string> pages;
 NSArray* files=[manager subpathsAtPath:assets];files=[files sortedArrayUsingSelector:@selector(localizedStandardCompare:)];
 for(NSString* relative in files){
  NSString* folder=relative.stringByDeletingLastPathComponent;
  if(!folder.length||![relative.pathExtension.lowercaseString isEqual:@"html"])continue;
  NSString* path=[assets stringByAppendingPathComponent:relative];NSDictionary* attrs=[manager attributesOfItemAtPath:path error:nil];
  if(![attrs[NSFileType] isEqual:NSFileTypeRegular]||[[relative pathComponents] containsObject:@".."])continue;
  NSString* encoded=[relative stringByAddingPercentEncodingWithAllowedCharacters:NSCharacterSet.URLPathAllowedCharacterSet];
  NSString* url=[@"u2k://" stringByAppendingString:encoded];
  std::ifstream input(path.UTF8String);if(!input)continue;std::stringstream content;content<<input.rdbuf();std::string html=content.str();

  pages[canonicalBuiltinUrl(url.UTF8String)]=html;
  [catalog addObject:@{@"group":[folder stringByReplacingOccurrencesOfString:@"_" withString:@" "],@"name":[relative.lastPathComponent.stringByDeletingPathExtension stringByReplacingOccurrencesOfString:@"_" withString:@" "],@"url":url}];
 }
 examples=[catalog copy];registerExamplePages(std::move(pages));
}
-(void)beginFrame:(int64_t)target fired:(int64_t)fired locked:(BOOL)locked{
 if(closing||!renderClock)return;const auto stamp=mediaSampleTime();for(auto& r:runtimes)r.second->browser->beginFrame(stamp);
 auto& c=*renderClock;c.latency+=.1*(double(stamp-fired)-c.latency);c.lead=std::clamp<int64_t>(std::llround(c.latency),0,int64_t(c.period/2));const auto delay=stamp-target;++c.sent;if(locked)++c.locked;c.sumDispatch+=delay;if(delay>c.maxDispatch)c.maxDispatch=delay;if(delay<c.minDispatch)c.minDispatch=delay;if(std::abs(double(delay))>c.period/2)++c.lateDispatch;
}
-(void)setup{
 [self scanExamples];
 scene=std::make_unique<Scene>();mixer=std::make_shared<AudioMixer>();output=makeOutput();output->syncDiagnostic(avLoopbackTest);output->referenceDiagnostic(referenceIds);output->audio(mixer);output->audioSourceAge(sourceAgeSamples);
 if(externalBeginFrame){renderClock=std::make_unique<RenderClock>();RenderClock* clock=renderClock.get();Output* out=output.get();__weak AppDelegate* owner=self;
  clock->thread=std::thread([clock,out,owner]{int64_t last=mediaSampleTime();
   while(!clock->quit){const double period=clock->period;const auto selection=out->videoSelectionAfter(last+int64_t(period/2),clock->subdivisions);
    int64_t target=selection?*selection:last+int64_t(std::llround(period));const auto now=mediaSampleTime();if(target<now-int64_t(period))target=now; // Idle start or stall: restart the grid.
    const auto fire=target-clock->lead.load();waitUntilSampleTime(fire);last=target;if(clock->quit)break;
    const BOOL locked=selection.has_value();dispatch_async(dispatch_get_main_queue(),^{AppDelegate* o=owner;if(o)[o beginFrame:target fired:fire locked:locked];});}});}
 clockGaps.observe(hostClockSnapshot());(void)mediaSampleTime();message=@"Ready";
 __weak AppDelegate* wakeOwner=self;audioWakeObserver=[NSWorkspace.sharedWorkspace.notificationCenter addObserverForName:NSWorkspaceDidWakeNotification object:nil queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification* notification){AppDelegate* owner=wakeOwner;if(owner)[owner audioConfigurationChanged];}];
 monitoredLayers=[NSMutableSet set];monitoredAFL=[NSMutableSet set];monitorMasterPFL=std::make_shared<std::atomic<bool>>(false);monitorMixer=std::make_shared<AudioMixer>();monitorMaster=std::make_shared<std::atomic<bool>>(false);monitorProgram=std::make_shared<std::atomic<bool>>(true);
 traceStarted=std::chrono::steady_clock::now();if(const char* path=std::getenv("U2K_AUDIO_TRACE")){audioTrace.open(path);if(!audioTrace)throw std::runtime_error("Cannot open U2K_AUDIO_TRACE");audioTrace<<"elapsed_s,layer,running,on_air,reads,missing_frames,no_source_reads,headroom_count,headroom_min_ms,headroom_mean_ms,headroom_max_ms,missing_total,api_errors,recoveries,buffered_frames,target_frames,late_video,dropped_video,source_age_budget_ms,last_read_age_ms,clock_resets,generation,duplicate_packets,reordered_packets,trimmed_frames,timestamp_adjustments,timestamp_gap_frames,stale_packets,clock_calibrated,startup_shift_ms,stream_vs_queue_phase_ms,scheduled_audio_frames,pre_source_padding_frames,gap_sequence,gap_generation,gap_missing,gap_start,gap_end,gap_read_host,gap_newest_end,gap_newest_arrival,gap_newest_published,gap_recovery_arrival,gap_recovery_published,gap_recovery_start,gap_recovery_end,preroll_audio_frames,preroll_video_frames,first_dropped_frame,startup_sync_frames,startup_sync_ms,startup_lead_frames,startup_audio_padding_frames,video_history_reads,video_history_misses,video_selection_age_ms,video_history_bytes,source_flash_count,source_flash_pts,source_pulse_count,source_pulse_pts,audio_source_origin,scheduled_flash_count,scheduled_flash_pts,video_history_startup_misses,video_prepared_reads,video_conversion_fallbacks,scheduled_activity,video_worker_qos,audio_rate_ppm,audio_estimated_rate_ppm,audio_phase_error_ms,audio_drift_ready,audio_drift_rejected,mapping_source,mapping_stream,mapping_ratio,reserve_frames,sync_offset_ms,filter_lookahead_ms,rate_uncertainty_ppm,video_repeats,video_skips,video_completion_gap_max_ms,video_frames_peak\n";}
 prefs=[@{@"device":@"",@"preferredDevice":@"",@"mode":@0,@"pixel":@"BGRA",@"keying":@YES,@"browserSize":@"match",@"width":@1920,@"height":@1080,@"layersVisible":@YES,@"timingProfile":@"reliable",@"syncOffsetMs":@0,@"audioMuted":@NO,@"audioGain":@1,@"audioMono":@NO,@"audioLeft":@YES,@"audioRight":@YES,@"viewMode":@"full",@"uiScale":@1,@"alwaysOnTop":@NO,@"monitorFrame":@""} mutableCopy];layers=[NSMutableArray array];
 BOOL hasSession=NO;
 if(!smoke){NSData* data=[NSData dataWithContentsOfFile:ns(settingsPath().string())];if(data){NSError* error=nil;id saved=[NSJSONSerialization JSONObjectWithData:data options:NSJSONReadingMutableContainers error:&error];if([saved isKindOfClass:NSDictionary.class]&&[saved[@"preferences"] isKindOfClass:NSDictionary.class]&&[saved[@"layers"] isKindOfClass:NSArray.class]){hasSession=YES;for(NSString* key in prefs.allKeys){id value=saved[@"preferences"][key];if([value isKindOfClass:[prefs[key] class]]||([prefs[key] isKindOfClass:NSNumber.class]&&[value isKindOfClass:NSNumber.class])||([prefs[key] isKindOfClass:NSString.class]&&[value isKindOfClass:NSString.class]))prefs[key]=value;}for(id l in saved[@"layers"]){if(![l isKindOfClass:NSDictionary.class])continue;NSMutableDictionary* v=[[self newLayer] mutableCopy];[v addEntriesFromDictionary:l];if([v[@"id"] isKindOfClass:NSString.class]&&[v[@"name"] isKindOfClass:NSString.class]&&[v[@"url"] isKindOfClass:NSString.class]&&allowedUrl([v[@"url"] UTF8String]))[layers addObject:v];}}else{auto backup=settingsPath();backup+=".invalid-backup";std::filesystem::copy_file(settingsPath(),backup,std::filesystem::copy_options::overwrite_existing);message=@"Invalid configuration preserved in session.json.invalid-backup";}}}
 if(!hasSession){NSMutableDictionary* first=[self newLayer];if(!smoke){auto old=settingsPath().parent_path()/"settings.txt";try{if(std::filesystem::exists(old)){auto legacy=Settings::load(old);first[@"url"]=ns(legacy.url);prefs[@"device"]=ns(legacy.device);prefs[@"mode"]=@(legacy.mode);prefs[@"browserSize"]=legacy.matchOutput?@"match":@"custom";prefs[@"width"]=@(legacy.viewport.width);prefs[@"height"]=@(legacy.viewport.height);}}catch(const std::exception& e){message=ns(e.what());}}[layers addObject:first];}
 // Migrate saved example URLs to the catalog's display spelling and shorter address.
 for(NSMutableDictionary* layer in layers)for(NSDictionary* example in examples)if(canonicalBuiltinUrl([layer[@"url"] UTF8String])==canonicalBuiltinUrl([example[@"url"] UTF8String])){layer[@"url"]=example[@"url"];break;}
 if(![@[@"match",@"1920x1080",@"3840x2160",@"custom"] containsObject:prefs[@"browserSize"]])prefs[@"browserSize"]=@"match";
 if(![@[@"reliable",@"low"] containsObject:prefs[@"timingProfile"]])prefs[@"timingProfile"]=@"reliable";
 if(!std::isfinite([prefs[@"syncOffsetMs"] doubleValue])||std::abs([prefs[@"syncOffsetMs"] doubleValue])>200||std::floor([prefs[@"syncOffsetMs"] doubleValue])!=[prefs[@"syncOffsetMs"] doubleValue])prefs[@"syncOffsetMs"]=@0;
 if(!requestedTimingProfile.empty())prefs[@"timingProfile"]=ns(requestedTimingProfile);if(requestedSyncOffset!=INT_MAX)prefs[@"syncOffsetMs"]=@(requestedSyncOffset);
 if(![@[@"BGRA",@"ARGB"] containsObject:prefs[@"pixel"]])prefs[@"pixel"]=@"BGRA";
 if(!std::isfinite([prefs[@"audioGain"] doubleValue])||[prefs[@"audioGain"] doubleValue]<0||[prefs[@"audioGain"] doubleValue]>2)prefs[@"audioGain"]=@1;
 if(![@[@"full",@"monitor"] containsObject:prefs[@"viewMode"]])prefs[@"viewMode"]=@"full";
 if(![@[@1,@.75,@.5] containsObject:prefs[@"uiScale"]])prefs[@"uiScale"]=@1;
 NSMutableSet* identities=[NSMutableSet set];for(NSMutableDictionary* layer in [layers copy]){if([identities containsObject:layer[@"id"]]||[layer[@"id"] length]<1||[layer[@"id"] length]>80||[layer[@"name"] length]<1||[layer[@"name"] length]>80||identities.count>=16){[layers removeObjectIdenticalTo:layer];continue;}[identities addObject:layer[@"id"]];for(NSString* key in @[@"visible",@"muted",@"mono"])if(![layer[key] isKindOfClass:NSNumber.class])layer[key]=@NO;if(![layer[@"gain"] isKindOfClass:NSNumber.class]||!std::isfinite([layer[@"gain"] doubleValue])||[layer[@"gain"] doubleValue]<0||[layer[@"gain"] doubleValue]>2)layer[@"gain"]=@1;}
 try{validate([self viewport]);}catch(...){prefs[@"browserSize"]=@"match";}
 if(![prefs[@"device"] isKindOfClass:NSString.class])prefs[@"device"]=@"";
 const double scale=[prefs[@"uiScale"] doubleValue];window=[[RackWindow alloc]initWithContentRect:NSMakeRect(0,0,std::round(1280*scale),std::round(460*scale)) styleMask:NSWindowStyleMaskBorderless|NSWindowStyleMaskResizable backing:NSBackingStoreBuffered defer:NO];window.title=@"URL2KeyFill";window.delegate=self;window.minSize=NSMakeSize(std::round(1050*scale),std::round(390*scale));window.opaque=NO;window.backgroundColor=NSColor.clearColor;window.contentView.wantsLayer=YES;window.contentView.layer.cornerRadius=13;window.contentView.layer.masksToBounds=YES;[window center];fullFrame=window.frame;
 WKWebViewConfiguration* configuration=[[WKWebViewConfiguration alloc]init];configuration.websiteDataStore=WKWebsiteDataStore.nonPersistentDataStore;[configuration.userContentController addScriptMessageHandler:self name:@"native"];web=[[RackWebView alloc]initWithFrame:window.contentView.bounds configuration:configuration];web.autoresizingMask=NSViewWidthSizable|NSViewHeightSizable;web.navigationDelegate=self;web.UIDelegate=self;[web setValue:@NO forKey:@"drawsBackground"];[window.contentView addSubview:web];
 preview=[[Preview alloc]initWithFrame:NSZeroRect];[window.contentView addSubview:preview];
 // No "Enter Full Screen" or tab items: the borderless rack has neither.
 [NSUserDefaults.standardUserDefaults setBool:NO forKey:@"NSFullScreenMenuItemEverywhere"];NSWindow.allowsAutomaticWindowTabbing=NO;
 NSMenu* menu=[[NSMenu alloc]init];NSMenuItem* appItem=[[NSMenuItem alloc]init];NSMenu* appMenu=[[NSMenu alloc]init];[appMenu addItemWithTitle:@"About URL2KeyFill" action:@selector(showAbout:) keyEquivalent:@""].target=self;[appMenu addItem:NSMenuItem.separatorItem];[appMenu addItemWithTitle:@"Quit URL2KeyFill" action:@selector(terminate:) keyEquivalent:@"q"];appItem.submenu=appMenu;[menu addItem:appItem];
 NSMenuItem* viewItem=[[NSMenuItem alloc]init];NSMenu* view=[[NSMenu alloc]initWithTitle:@"View"];for(NSArray* e in @[@[@"Rack Size",@"showActualSize:",@"1"],@[@"Small Size",@"showThreeQuarters:",@"2"],@[@"Tiny Size",@"showHalf:",@"3"],@[@"Just Monitor",@"showMonitor:",@"4"],@[]]){if(!e.count){[view addItem:NSMenuItem.separatorItem];continue;}[view addItemWithTitle:e[0] action:NSSelectorFromString(e[1]) keyEquivalent:e[2]].target=self;}
 NSMenuItem* onTop=[view addItemWithTitle:@"Always on Top" action:@selector(toggleOnTop:) keyEquivalent:@"t"];onTop.keyEquivalentModifierMask=NSEventModifierFlagCommand|NSEventModifierFlagOption;onTop.target=self;view.delegate=self;viewItem.submenu=view;[menu addItem:viewItem];NSApp.mainMenu=menu;[self applyView];[self applyOnTop];

 [self refresh];if(smoke){prefs[@"mode"]=@(hardwareTestMode);prefs[@"browserSize"]=@"1920x1080";[self chooseMode];}if(avLoopbackTest){layers.firstObject[@"url"]=avTestUrl.empty()?@"u2k://Diagnostics/10_Sincronismo_AV.html":ns(avTestUrl);layers.firstObject[@"name"]=@"SDI loopback A/V";avLayerId=layers.firstObject[@"id"];if(!avOverlayUrl.empty()){NSMutableDictionary* overlay=[self newLayer];overlay[@"url"]=ns(avOverlayUrl);overlay[@"name"]=@"SDI loopback overlay";[layers insertObject:overlay atIndex:0];}}for(NSDictionary* layer in layers)[self createRuntime:layer];[self configureScene];
 NSURL* ui=[NSBundle.mainBundle URLForResource:@"interface" withExtension:@"html"];[web loadFileURL:ui allowingReadAccessToURL:ui];
 timer=[NSTimer scheduledTimerWithTimeInterval:.05 target:self selector:@selector(update:) userInfo:nil repeats:YES];started=std::chrono::steady_clock::now();measured=started;[window makeKeyAndOrderFront:nil];[NSApp activateIgnoringOtherApps:YES];
}
// Views: the rack is the main view, optionally scaled; Monitor Only shows just the preview.
// AppKit may still insert "Enter Full Screen" when the menu opens; drop it.
-(void)menuNeedsUpdate:(NSMenu*)menu{for(NSMenuItem* item in menu.itemArray.copy)if(item.action==@selector(toggleFullScreen:))[menu removeItem:item];}
// Rack Size ⌘1 (⌘0 too, handled by the window) 100%, Small Size ⌘2 75%, Tiny Size ⌘3 50%:
// always the rack view. Just Monitor ⌘4.
-(void)showSize:(double)scale{if([prefs[@"viewMode"] isEqual:@"monitor"])[self setViewMode:@"full"];[self setScale:scale];}
-(void)showActualSize:(id)sender{[self showSize:1];}
-(void)showThreeQuarters:(id)sender{[self showSize:.75];}
-(void)showHalf:(id)sender{[self showSize:.5];}
-(void)showMonitor:(id)sender{[self setViewMode:@"monitor"];}
-(void)toggleOnTop:(id)sender{prefs[@"alwaysOnTop"]=@(![prefs[@"alwaysOnTop"] boolValue]);[self applyOnTop];[self scheduleSave];[self sendState];}
-(BOOL)validateMenuItem:(NSMenuItem*)item{
 const bool solo=[prefs[@"viewMode"] isEqual:@"monitor"];const double scale=[prefs[@"uiScale"] doubleValue];const SEL action=item.action;
 const auto mark=[&](SEL a,double s){if(action==a)item.state=!solo&&scale==s?NSControlStateValueOn:NSControlStateValueOff;};
 mark(@selector(showActualSize:),1);mark(@selector(showThreeQuarters:),.75);mark(@selector(showHalf:),.5);
 if(action==@selector(showMonitor:))item.state=solo?NSControlStateValueOn:NSControlStateValueOff;
 if(action==@selector(toggleOnTop:))item.state=[prefs[@"alwaysOnTop"] boolValue]?NSControlStateValueOn:NSControlStateValueOff;
 return YES;
}
-(void)setScale:(double)scale{
 scale=std::clamp(scale,.5,1.);const double old=[prefs[@"uiScale"] doubleValue];if(scale==old||[prefs[@"viewMode"] isEqual:@"monitor"])return;
 prefs[@"uiScale"]=@(scale);window.minSize=NSMakeSize(std::round(1050*scale),std::round(390*scale));
 // Keep the top-left corner; the page scales itself with a CSS transform and reports
 // the scaled height. WKWebView pageZoom is not used: WebKit keeps zoomed text at 9 px
 // or more, so text outgrew its controls at the smaller scales.
 NSRect f=window.frame;const double top=NSMaxY(f);f.size=NSMakeSize(std::round(f.size.width*scale/old),std::round(f.size.height*scale/old));f.origin.y=top-f.size.height;[window setFrame:f display:YES];
 [self scheduleSave];[self sendState];
}
-(void)setViewMode:(NSString*)mode{
 if([prefs[@"viewMode"] isEqual:mode])return;
 if([mode isEqual:@"monitor"])fullFrame=window.frame;else prefs[@"monitorFrame"]=NSStringFromRect(window.frame);
 prefs[@"viewMode"]=mode;[self applyView];[self scheduleSave];[self sendState];
}
-(void)applyView{
 const bool solo=[prefs[@"viewMode"] isEqual:@"monitor"];const double scale=[prefs[@"uiScale"] doubleValue];
 // Monitor Only puts the preview under the transparent page so its overlay controls stay on top.
 [preview removeFromSuperview];[window.contentView addSubview:preview positioned:solo?NSWindowBelow:NSWindowAbove relativeTo:web];
 if(!solo){window.contentResizeIncrements=NSMakeSize(1,1);window.minSize=NSMakeSize(std::round(1050*scale),std::round(390*scale));[window setFrame:fullFrame display:YES];return;}
 window.minSize=NSMakeSize(320,180);window.contentAspectRatio=NSMakeSize(16,9);
 NSRect f=NSRectFromString(prefs[@"monitorFrame"]);bool visible=false;for(NSScreen* screen in NSScreen.screens)if(NSIntersectsRect(NSInsetRect(screen.visibleFrame,40,40),f))visible=true;
 if(f.size.width<320||f.size.height<180||!visible)f=NSMakeRect(NSMinX(window.frame),NSMaxY(window.frame)-270,480,270);
 [window setFrame:f display:YES];
}
-(void)applyOnTop{
 const bool on=[prefs[@"alwaysOnTop"] boolValue];window.level=on?NSFloatingWindowLevel:NSNormalWindowLevel;
 window.collectionBehavior=on?NSWindowCollectionBehaviorCanJoinAllSpaces|NSWindowCollectionBehaviorFullScreenAuxiliary:NSWindowCollectionBehaviorDefault;
}
-(void)windowDidMove:(NSNotification*)notification{if([prefs[@"viewMode"] isEqual:@"monitor"]){prefs[@"monitorFrame"]=NSStringFromRect(window.frame);[self scheduleSave];}}
-(void)windowDidResize:(NSNotification*)notification{[self windowDidMove:notification];}
-(void)showAbout:(id)sender{NSString* credits=[NSString stringWithFormat:@"Browser engine: CEF %s",CEF_VERSION];[NSApp orderFrontStandardAboutPanelWithOptions:@{NSAboutPanelOptionCredits:[[NSAttributedString alloc]initWithString:credits attributes:@{NSFontAttributeName:[NSFont systemFontOfSize:NSFont.smallSystemFontSize],NSForegroundColorAttributeName:NSColor.secondaryLabelColor}]}];[NSApp activateIgnoringOtherApps:YES];}
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
 if([type isEqual:@"layout"]){const bool solo=[prefs[@"viewMode"] isEqual:@"monitor"];double h=[data[@"heightWindow"] doubleValue];if(!solo&&std::isfinite(h)&&h>=150&&h<2000){NSRect f=window.frame;double desired=std::min(std::round(h),NSScreen.mainScreen.visibleFrame.size.height-30);if(std::abs(f.size.height-desired)>2){f.origin.y+=f.size.height-desired;f.size.height=desired;[window setFrame:f display:YES];}}
  // Page coordinates are CSS pixels; page zoom scales them to view points.
  NSRect r=NSMakeRect([data[@"x"] doubleValue],window.contentView.bounds.size.height-([data[@"y"] doubleValue]+[data[@"height"] doubleValue]),[data[@"width"] doubleValue],[data[@"height"] doubleValue]);if(r.size.width>=0&&r.size.width<10000&&r.size.height>=0&&r.size.height<10000)preview.frame=r;preview.hidden=![data[@"show"] boolValue];return;}
 if([type isEqual:@"drag"]){dragging=YES;dragScreen=NSEvent.mouseLocation;dragOrigin=window.frame.origin;return;}
 if([type isEqual:@"view"]){NSString* mode=data[@"mode"];if([mode isKindOfClass:NSString.class]&&[@[@"full",@"monitor"] containsObject:mode])[self setViewMode:mode];return;}
 if([type isEqual:@"alwaysOnTop"]){[self toggleOnTop:nil];return;}
 if([type isEqual:@"requestQuit"]){[self requestQuit];return;}
 if([type isEqual:@"quit"]){[self quit];return;}
 if([type isEqual:@"monitor"]){preview->key=[data[@"key"] boolValue];[preview setNeedsDisplay:YES];return;}
 if([type isEqual:@"refresh"]){[self refresh];}
 else if([type isEqual:@"play"]){if(output->status().running){[self stopSDI];wasRunning=NO;message=@"SDI output stopped";}else{auto m=[self selectedMode];if(!m)throw std::runtime_error("No compatible output mode. Open SETUP.");Mode requested=*m;requested.keying=[prefs[@"keying"] boolValue];[self startSDI:&requested];wasRunning=YES;message=@"SDI output started";}}
 else if([type isEqual:@"audioGlobal"]){if(data[@"gain"]){double gain=[data[@"gain"] doubleValue];if(!std::isfinite(gain)||gain<0||gain>2)throw std::runtime_error("Invalid output gain");prefs[@"audioGain"]=@(gain);}for(NSString* key in @[@"mono",@"left",@"right"]){NSString* target=[@"audio" stringByAppendingString:[key capitalizedString]];if(data[key])prefs[target]=@([data[key] boolValue]);}}
 else if([type isEqual:@"audioMonitor"]){NSString* identity=data[@"id"];NSString* mode=data[@"mode"]?:([identity isEqual:@"output"]?@"afl":@"pfl");if(![identity isKindOfClass:NSString.class]||(![mode isEqual:@"pfl"]&&![mode isEqual:@"afl"]))throw std::runtime_error("Invalid monitor source or mode");BOOL pfl=[mode isEqual:@"pfl"];
 if([identity isEqual:@"output"]){monitorOutput=!(monitorOutput&&monitorOutputPFL==pfl);monitorOutputPFL=pfl;[monitoredLayers removeAllObjects];[monitoredAFL removeAllObjects];}
 else{BOOL valid=NO;for(NSDictionary* l in layers)if([l[@"id"] isEqual:identity])valid=YES;if(!valid)throw std::runtime_error("Unknown monitor layer");monitorOutput=NO;BOOL same=[monitoredLayers containsObject:identity]&&([monitoredAFL containsObject:identity]!=pfl);if(same){[monitoredLayers removeObject:identity];[monitoredAFL removeObject:identity];}else{[monitoredLayers addObject:identity];if(pfl)[monitoredAFL removeObject:identity];else [monitoredAFL addObject:identity];}}
 [self configureAudio];[self startAudioMonitor];}
 else if([type isEqual:@"audioMute"]){prefs[@"audioMuted"]=@([data[@"value"] boolValue]);message=[prefs[@"audioMuted"] boolValue]?@"Master audio muted":@"Master audio restored";}
 else if([type isEqual:@"pause"]){if(output->status().running){bool air=!output->status().onAir;output->onAir(air);message=air?@"Image and audio restored":@"Paused · Image transparent, audio silent, SDI active";}}
 else if([type isEqual:@"layersVisible"]){prefs[@"layersVisible"]=@([data[@"value"] boolValue]);}
 else if([type isEqual:@"add"]){if(layers.count>=16)throw std::runtime_error("This development build supports up to 16 layers. Measure resource use before adding more.");NSMutableDictionary* l=[self newLayer];l[@"name"]=@"New layer";l[@"visible"]=@NO;[layers insertObject:l atIndex:0];[self createRuntime:l];prefs[@"layersVisible"]=@YES;openLayerId=l[@"id"];message=@"Test layer added at top";}
 else if([type isEqual:@"reload"]){for(NSDictionary* layer in layers)if(!data[@"id"]||[layer[@"id"] isEqual:data[@"id"]])[self reloadLayer:layer];message=data[@"id"]?@"Layer reloaded":@"All layers reloaded";}
 else if([type isEqual:@"delete"]){for(NSUInteger i=0;i<layers.count;i++)if([layers[i][@"id"] isEqual:data[@"id"]]){auto it=runtimes.find([data[@"id"] UTF8String]);if(it!=runtimes.end()){it->second->browser->close();retired.push_back(it->second);runtimes.erase(it);}[layers removeObjectAtIndex:i];message=@"Layer deleted";break;}}
 else if([type isEqual:@"layer"]){for(NSMutableDictionary* layer in layers)if([layer[@"id"] isEqual:data[@"id"]]){
  if(data[@"name"]){if(![data[@"name"] isKindOfClass:NSString.class]||[data[@"name"] length]<1||[data[@"name"] length]>80)throw std::runtime_error("Layer name must contain 1–80 characters.");layer[@"name"]=data[@"name"];}
  if(data[@"url"]){if(![data[@"url"] isKindOfClass:NSString.class]||!allowedUrl([data[@"url"] UTF8String]))throw std::runtime_error("Use an HTTP/HTTPS URL or a built-in example.");if(![layer[@"url"] isEqual:data[@"url"]]){layer[@"url"]=data[@"url"];[self reloadLayer:layer];message=@"Layer URL updated and reloaded";}}
  for(NSString* key in @[@"visible",@"muted",@"mono",@"left",@"right"])if(data[key])layer[key]=@([data[key] boolValue]);if(data[@"gain"]){double gain=[data[@"gain"] doubleValue];if(!std::isfinite(gain)||gain<0||gain>2)throw std::runtime_error("Gain must be between 0 and 2.");layer[@"gain"]=@(gain);}break;}}
 else if([type isEqual:@"reorder"]){NSArray* ids=data[@"ids"];if(![ids isKindOfClass:NSArray.class]||ids.count!=layers.count)throw std::runtime_error("Invalid layer order.");NSMutableArray* ordered=[NSMutableArray array];for(id identity in ids){for(NSDictionary* l in layers)if([l[@"id"] isEqual:identity]&&![ordered containsObject:l])[ordered addObject:l];}if(ordered.count!=layers.count)throw std::runtime_error("Invalid layer order.");layers=ordered;message=@"Layer order updated";}
 else if([type isEqual:@"syncAdjustment"]){const double value=[data[@"ms"] doubleValue];if(!std::isfinite(value)||std::floor(value)!=value||std::abs(value)>200)throw std::runtime_error("A/V adjustment must be -200..200 ms.");output->syncAdjustment([self effectiveSyncSamples:int(value)]);prefs[@"syncOffsetMs"]=@(int(value));message=@"A/V adjustment updated";}
 else if([type isEqual:@"configure"]){if(output->status().running)throw std::runtime_error("Stop PLAY before changing output settings.");NSMutableDictionary* next=[prefs mutableCopy];NSString* device=data[@"device"];bool valid=false;for(auto& d:devices)if(d.id==[device UTF8String])valid=true;if(!valid)throw std::runtime_error("Select a detected device.");next[@"device"]=device;next[@"preferredDevice"]=device;next[@"mode"]=@([data[@"mode"] unsignedIntValue]);if(![@[@"BGRA",@"ARGB"] containsObject:data[@"pixel"]])throw std::runtime_error("Unsupported pixel format.");next[@"pixel"]=data[@"pixel"];next[@"keying"]=@([data[@"keying"] boolValue]);if(![@[@"match",@"1920x1080",@"3840x2160",@"custom"] containsObject:data[@"browserSize"]])throw std::runtime_error("Unsupported browser size.");next[@"browserSize"]=data[@"browserSize"];next[@"width"]=@([data[@"width"] intValue]);next[@"height"]=@([data[@"height"] intValue]);validate({[next[@"width"] intValue],[next[@"height"] intValue]});next[@"audioMuted"]=@([data[@"audioMuted"] boolValue]);if(data[@"timingProfile"]){if(![@[@"reliable",@"low"] containsObject:data[@"timingProfile"]])throw std::runtime_error("Invalid timing profile.");next[@"timingProfile"]=data[@"timingProfile"];}if(data[@"syncOffsetMs"]){const double offset=[data[@"syncOffsetMs"] doubleValue];if(!std::isfinite(offset)||std::abs(offset)>200||std::floor(offset)!=offset)throw std::runtime_error("A/V adjustment must be an integer from -200 to 200 ms.");next[@"syncOffsetMs"]=@(int(offset));}u2k::Size old=[self viewport];BOOL deviceChanged=![next[@"device"] isEqual:prefs[@"device"]];prefs=next;[self chooseMode];if(deviceChanged)if(auto m=[self selectedMode])prefs[@"keying"]=@(m->keying);if(!(old==[self viewport]))for(NSDictionary* layer in layers)[self reloadLayer:layer];message=@"Configuration saved";}
 BOOL imageChanged=[@[@"add",@"delete",@"reorder",@"configure",@"reload",@"refresh"] containsObject:type]||([type isEqual:@"layer"]&&(data[@"visible"]||data[@"url"]));
 if(imageChanged)[self configureScene];else [self configureAudio];
 if([@[@"audioGlobal",@"audioMute",@"syncAdjustment",@"layer",@"layersVisible",@"add",@"delete",@"reorder",@"configure"] containsObject:type])[self scheduleSave];
 [self sendState];
 }catch(const std::exception& e){message=ns(e.what());[self sendState];}
}
-(void)stopAudioMonitor{
 if(audioConfigurationObserver){[NSNotificationCenter.defaultCenter removeObserver:audioConfigurationObserver];audioConfigurationObserver=nil;}
 [audioEngine stop];audioSource=nil;audioEngine=nil;
}
-(void)startAudioMonitor{
 if(audioEngine||closing||(!monitorOutput&&!monitoredLayers.count))return;
 audioEngine=[AVAudioEngine new];auto mainMix=mixer,localMix=monitorMixer;auto master=monitorMaster,program=monitorProgram,preMaster=monitorMasterPFL;auto cursor=std::make_shared<int64_t>(0);audioSource=[[AVAudioSourceNode alloc]initWithFormat:[[AVAudioFormat alloc]initStandardFormatWithSampleRate:48000 channels:2] renderBlock:^OSStatus(BOOL* silent,const AudioTimeStamp* time,AVAudioFrameCount count,AudioBufferList* buffers){const int64_t now=mediaSampleTime()-4800;if(!*cursor||std::abs(*cursor-now)>4800)*cursor=now;auto samples=master->load()?(program->load()?(preMaster->load()?mainMix->samplePreMaster(*cursor,count):mainMix->sample(*cursor,count)):std::vector<float>(count*2,0)):localMix->sample(*cursor,count);*cursor+=count;for(UInt32 c=0;c<buffers->mNumberBuffers;c++){float* dst=(float*)buffers->mBuffers[c].mData;for(UInt32 j=0;j<count;j++)dst[j]=std::clamp(samples[j*2+std::min(c,1u)],-1.f,1.f);}*silent=NO;return noErr;}];[audioEngine attachNode:audioSource];[audioEngine connect:audioSource to:audioEngine.mainMixerNode format:[[AVAudioFormat alloc]initStandardFormatWithSampleRate:48000 channels:2]];NSError* error=nil;if(![audioEngine startAndReturnError:&error]){audioEngine=nil;audioSource=nil;monitorOutput=NO;[monitoredLayers removeAllObjects];[monitoredAFL removeAllObjects];throw std::runtime_error(error.localizedDescription.UTF8String?:"Audio monitor unavailable");}
 __weak AppDelegate* weakSelf=self;
 audioConfigurationObserver=[NSNotificationCenter.defaultCenter addObserverForName:AVAudioEngineConfigurationChangeNotification object:audioEngine queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification* notification){AppDelegate* owner=weakSelf;if(owner&&notification.object==owner->audioEngine)[owner audioConfigurationChanged];}];
}
-(void)audioConfigurationChanged{
 if(closing)return;[self stopAudioMonitor];
 try{[self startAudioMonitor];}catch(const std::exception& e){message=ns(e.what());}
 [self configureAudio];[self sendState];
}
-(void)recordAudioTelemetry{
 const auto status=output->status();const double elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-traceStarted).count();
 for(auto& entry:runtimes){auto w=entry.second->audio->takeReadWindow();reportedAudio[entry.first]=w;if(!audioTrace.is_open())continue;
  audioTrace<<std::fixed<<std::setprecision(6)<<elapsed<<','<<entry.first<<','<<status.running<<','<<status.onAir<<','<<w.reads<<','<<w.missing<<','<<w.noSource<<','<<w.headroomCount<<',';
  if(w.headroomCount)audioTrace<<w.headroomMin/48.;audioTrace<<',';
  if(w.headroomCount)audioTrace<<double(w.headroomSum)/w.headroomCount/48.;audioTrace<<',';
  if(w.headroomCount)audioTrace<<w.headroomMax/48.;
  audioTrace<<','<<entry.second->audio->missingFrames<<','<<status.audioErrors<<','<<status.audioRecoveries<<','<<status.audioBuffered<<','<<status.audioTarget<<','<<status.late<<','<<status.dropped<<','<<status.audioSourceAge/48.<<',';if(status.audioReadAge!=INT64_MIN)audioTrace<<status.audioReadAge/48.;audioTrace<<','<<clockResets<<','<<entry.second->audio->generation<<','<<entry.second->audio->duplicatePackets<<','<<entry.second->audio->reorderedPackets<<','<<entry.second->audio->trimmedFrames<<','<<entry.second->audio->timestampAdjustments<<','<<entry.second->audio->timestampGapFrames<<','<<entry.second->audio->stalePackets<<','<<status.audioClockCalibrated<<','<<status.audioStartupShift/48.<<','<<status.audioStreamQueuePhase/48.<<','<<status.audioFrames<<','<<entry.second->audio->preSourcePaddingFrames;
  const auto& g=w.gap;audioTrace<<','<<g.sequence<<','<<g.generation<<','<<g.missing<<','<<g.start<<','<<g.end<<','<<g.readHost<<','<<g.newestEnd<<','<<g.newestArrival<<','<<g.newestPublished<<','<<g.recoveryArrival<<','<<g.recoveryPublished<<','<<g.recoveryStart<<','<<g.recoveryEnd<<','<<status.prerollAudio<<','<<status.prerollVideo<<','<<status.firstDroppedFrame<<','<<status.startupSyncFrames<<','<<status.startupSyncSamples/48.<<','<<status.startupLeadFrames<<','<<status.startupAudioPadding<<','<<status.videoHistoryReads<<','<<status.videoHistoryMisses<<',';if(status.videoSelectionAge!=INT64_MIN)audioTrace<<status.videoSelectionAge/48.;audioTrace<<','<<status.videoHistoryBytes<<','<<entry.second->browser->syncFlashCount<<',';if(entry.second->browser->syncFlashPts!=INT64_MIN)audioTrace<<entry.second->browser->syncFlashPts;audioTrace<<','<<entry.second->audio->syncPulseCount<<',';if(entry.second->audio->syncPulsePts!=INT64_MIN)audioTrace<<entry.second->audio->syncPulsePts;audioTrace<<','<<status.audioSourceOrigin<<','<<status.scheduledFlashCount<<',';if(status.scheduledFlashPts!=INT64_MIN)audioTrace<<status.scheduledFlashPts;audioTrace<<','<<status.videoHistoryStartupMisses<<','<<status.videoPreparedReads<<','<<status.videoConversionFallbacks<<','<<(playbackActivity?1:0)<<','<<status.videoWorkerQos<<','<<status.audioRatePpm<<','<<status.audioEstimatedRatePpm<<','<<status.audioPhaseError/48.<<','<<status.audioDriftReady<<','<<status.audioDriftRejected<<','<<status.audioMapping.source<<','<<status.audioMapping.stream<<','<<std::setprecision(12)<<status.audioMapping.ratio<<std::setprecision(6)<<','<<status.reserveFrames<<','<<status.syncOffsetSamples/48.<<','<<AudioResampler::right/48.<<','<<status.audioRateUncertaintyPpm<<','<<status.videoRepeats<<','<<status.videoSkips<<','<<status.videoCompletionGapMax/48.<<','<<status.videoFramesPeak<<'\n';
 }
 if(audioTrace.is_open()){audioTrace.flush();if(!audioTrace){audioTrace.close();message=@"Audio telemetry write failed";}}
}
-(void)sendState{
 if(!ready||closing)return;
 NSMutableArray* deviceList=[NSMutableArray array];for(auto& d:devices)[deviceList addObject:@{@"id":ns(d.id),@"name":ns(d.name)}];NSMutableArray* modeList=[NSMutableArray array],*pixels=[NSMutableArray array],*outputs=[NSMutableArray array];std::vector<uint32_t> seen;
 for(auto& d:devices)if(d.id==[prefs[@"device"] UTF8String])for(auto& m:d.modes){if(std::find(seen.begin(),seen.end(),m.id)==seen.end()){seen.push_back(m.id);[modeList addObject:@{@"id":@(m.id),@"name":ns(m.name.substr(0,m.name.find(" · ")))}];}if(m.id==[prefs[@"mode"] unsignedIntValue]){NSString* p=m.pixelFormat==0x42475241?@"BGRA":@"ARGB";if(![pixels containsObject:p])[pixels addObject:p];}}
 Mode* mode=[self selectedMode];if(mode){if(mode->keying)[outputs addObject:@"Fill + Key"];if(mode->fillOnlySupported)[outputs addObject:@"Fill only"];}
 auto status=output->status();u2k::Size size=[self viewport];NSString* deviceName=@"";for(auto& d:devices)if(d.id==[prefs[@"device"] UTF8String])deviceName=ns(d.name);
 uint64_t captured=0,unusedExpired=0,overruns=0,missingFrames=0;NSMutableArray* audioWindows=[NSMutableArray array];NSMutableArray* pageStates=[NSMutableArray array];for(auto& entry:runtimes){captured+=entry.second->audio->received;unusedExpired+=entry.second->audio->unusedExpired;overruns+=entry.second->audio->outputOverruns;missingFrames+=entry.second->audio->missingFrames;if(entry.second->audio->outputReads){const auto head=entry.second->audio->sourceHeadroom.load();[audioWindows addObject:[NSString stringWithFormat:@"%@: %@",ns(entry.first),head==INT64_MIN?@"no source coverage":[NSString stringWithFormat:@"%.2f ms",head/48.]]];}[pageStates addObject:entry.second->browser->audioClockMismatch?[ns(entry.second->browser->message) stringByAppendingString:@" · Audio rejected: unexpected CEF timestamp clock"]:ns(entry.second->browser->message)];}
 std::ostringstream metrics;metrics<<std::fixed<<std::setprecision(1)<<"CEF "<<browserRate<<" fps · Composite "<<composeRate<<" fps · SDI "<<sdIRate<<" fps · Browser "<<size.width<<" × "<<size.height<<" · ";if(mode)metrics<<"SDI "<<mode->rate.label()<<" · ";metrics<<"Composites "<<scene->composed<<" · SDI frames "<<status.completed<<" · Late "<<status.late<<" · Dropped "<<status.dropped<<" · Audio captured "<<captured<<" / SDI "<<status.audioFrames<<" frames · Audio errors "<<status.audioErrors<<" · Audio missing source frames "<<missingFrames<<" · Audio buffer overruns "<<overruns<<" · Audio unused expired "<<unusedExpired<<" · Audible audio samples "<<mixer->nonSilent<<" · Limited samples "<<mixer->clipped;metrics<<" · Audio clock recoveries "<<status.audioRecoveries<<" · Source read offset "<<status.audioSourceAge/48.<<" ms · Clock resets "<<clockResets;if(status.audioReadAge!=INT64_MIN)metrics<<" · Last source window age "<<status.audioReadAge/48.<<" ms";
 metrics<<" · Clock correction "<<status.audioRatePpm<<" ppm · Clock phase "<<status.audioPhaseError/48.<<" ms · Timing profile "<<([prefs[@"timingProfile"] isEqual:@"low"]?"Low latency":"Reliable")<<" · A/V adjustment "<<effectiveSyncMs([self calibratedSync],[prefs[@"syncOffsetMs"] intValue])<<" ms (calibrated "<<([self calibratedSync]?std::to_string(*[self calibratedSync]):std::string("none"))<<" + manual "<<[prefs[@"syncOffsetMs"] intValue]<<") · Video repeats "<<status.videoRepeats<<" · Video skips "<<status.videoSkips;
 NSString* detail=[[pageStates componentsJoinedByString:@" | "] stringByAppendingFormat:@"\nSource headroom (last read, per layer): %@",[audioWindows componentsJoinedByString:@" | "]];NSString* diagnostics=[ns(metrics.str()) stringByAppendingFormat:@"\n%@",detail];
 NSDictionary* state=@{@"examples":examples?:@[],@"layers":layers,@"layersVisible":prefs[@"layersVisible"],@"device":prefs[@"device"],@"deviceName":deviceName,@"devices":deviceList,@"mode":prefs[@"mode"],@"modeName":mode?ns(mode->name.substr(0,mode->name.find(" · "))):@"Unavailable",@"modes":modeList,@"pixels":pixels,@"pixel":prefs[@"pixel"],@"outputs":outputs,@"keying":prefs[@"keying"],@"width":@(size.width),@"height":@(size.height),@"browserSize":prefs[@"browserSize"],@"timingProfile":prefs[@"timingProfile"],@"syncCalibratedMs":[self calibratedSync]?@(*[self calibratedSync]):[NSNull null],@"version":appVersion(),@"viewMode":prefs[@"viewMode"],@"uiScale":prefs[@"uiScale"],@"alwaysOnTop":prefs[@"alwaysOnTop"],@"syncOffsetMs":prefs[@"syncOffsetMs"],@"audioMuted":prefs[@"audioMuted"],@"audioGain":prefs[@"audioGain"]?:@1,@"audioMono":prefs[@"audioMono"]?:@NO,@"audioLeft":prefs[@"audioLeft"]?:@YES,@"audioRight":prefs[@"audioRight"]?:@YES,@"monitorOutput":@(monitorOutput),@"monitoredLayers":monitoredLayers.allObjects,@"monitoredAFL":monitoredAFL.allObjects,@"monitorOutputPFL":@(monitorOutputPFL),@"output":@{@"running":@(status.running),@"onAir":@(status.onAir)},@"diagnostics":diagnostics,@"message":message?:@"Ready",@"openSetup":@(openSetup),@"openLayerId":openLayerId?:@""};
 openSetup=NO;openLayerId=nil;NSData* json=[NSJSONSerialization dataWithJSONObject:state options:0 error:nil];NSString* script=[@"window.receiveState(" stringByAppendingFormat:@"%@);",[[NSString alloc]initWithData:json encoding:NSUTF8StringEncoding]];[web evaluateJavaScript:script completionHandler:^(id value,NSError* error){if(error)std::cerr<<"UI state error: "<<error.localizedDescription.UTF8String<<'\n';}];
}
-(void)sendAudioLevels{
 if(!ready||closing)return;NSMutableDictionary* levels=[NSMutableDictionary dictionary];
 const int64_t start=mediaSampleTime()-14400;const size_t frames=14400;
 for(NSDictionary* layer in layers){auto runtime=runtimes.find([layer[@"id"] UTF8String]);if(runtime==runtimes.end())continue;auto level=runtime->second->audio->sourceLevels(start,frames,[layer[@"mono"] boolValue],[layer[@"left"] boolValue],[layer[@"right"] boolValue]);levels[layer[@"id"]]=@[@(level.rmsLeft),@(level.rmsRight),@(level.peakLeft),@(level.peakRight)];}
 auto composite=mixer->sourceLevels(start,frames);
 NSDictionary* packet=@{@"layers":levels,@"composite":@[@(composite.rmsLeft),@(composite.rmsRight),@(composite.peakLeft),@(composite.peakRight)]};
 NSData* json=[NSJSONSerialization dataWithJSONObject:packet options:0 error:nil];if(!json)return;
 NSString* script=[NSString stringWithFormat:@"(()=>{const meter=%@;window.receiveAudioLevels(meter.layers,meter.composite);})()",[[NSString alloc]initWithData:json encoding:NSUTF8StringEncoding]];[web evaluateJavaScript:script completionHandler:nil];
}
-(void)update:(NSTimer*)sender{
 if(smoke&&hardwareTest&&!diagnosticStopFile.empty()&&std::filesystem::exists(diagnosticStopFile)&&!closing){std::cerr<<"SYNC_DIAGNOSTIC_STOP host_samples="<<mediaSampleTime()<<'\n';[self quit];return;}
 if(dragging){if(!(NSEvent.pressedMouseButtons&1))dragging=NO;else{NSPoint p=NSEvent.mouseLocation;[window setFrameOrigin:NSMakePoint(dragOrigin.x+p.x-dragScreen.x,dragOrigin.y+p.y-dragScreen.y)];}}
 retired.erase(std::remove_if(retired.begin(),retired.end(),[](const auto& r){return r->closed;}),retired.end());
 if(closing){bool all=true;for(auto& r:runtimes)if(!r.second->closed)all=false;for(auto& r:retired)if(!r->closed)all=false;if(all){[timer invalidate];CefQuitMessageLoop();}return;}
 if(clockGaps.observe(hostClockSnapshot())){++clockResets;[self stopSDI];wasRunning=NO;for(auto& entry:runtimes)entry.second->audio->clear();[self audioConfigurationChanged];message=@"Sleep/wake detected · audio history reset · PLAY stopped";}
 auto status=output->status();if(wasRunning&&!status.running){[self stopSDI];wasRunning=NO;message=ns(status.message);openSetup=YES;[self setViewMode:@"full"];}
 *monitorProgram=!(status.running&&!status.onAir);[self setAudioOutputActive:status.running&&status.onAir];
 auto image=scene->frames.get();if(status.running&&!status.onAir)image=nullptr;if(image!=preview->frame){preview->frame=image;[preview setNeedsDisplay:YES];}
 auto now=std::chrono::steady_clock::now();double dt=std::chrono::duration<double>(now-measured).count();if(dt>=1){uint64_t paints=0;for(auto& r:runtimes)paints+=r.second->browser->paints();browserRate=paints>=lastSequence?(paints-lastSequence)/dt:0;composeRate=(scene->composed-lastComposed)/dt;sdIRate=status.completed>=lastCompleted?(status.completed-lastCompleted)/dt:0;lastSequence=paints;lastComposed=scene->composed;lastCompleted=status.completed;measured=now;[self recordAudioTelemetry];}
 [self sendAudioLevels];
 if(++tick%4==0)[self sendState];
 if(smoke){
  if(hardwareTest&&smokePhase==4&&!syncCalibrationFile.empty()&&tick%20==0){std::ifstream input(syncCalibrationFile);if(input){std::string line;std::getline(input,line);char* end=nullptr;const long n=std::strtol(line.c_str(),&end,10);if(line.empty()||line.size()>32||*end||n< -200||n>200){std::cerr<<"FAIL invalid calibration file\n";[self quit];return;}if(n!=syncCalibrationLast){try{output->syncAdjustment([self effectiveSyncSamples:int(n)]);}catch(const std::exception& e){std::cerr<<"FAIL calibration adjustment: "<<e.what()<<std::endl;[self quit];return;}prefs[@"syncOffsetMs"]=@(int(n));syncCalibrationLast=int(n);std::cout<<"SYNC_CALIBRATION ms="<<n<<" active_s="<<std::chrono::duration<double>(now-started).count()<<std::endl;}}}
  if(hardwareTest&&smokePhase==4&&changedSyncOffset!=INT_MAX&&!syncChanged&&std::chrono::duration<double>(now-started).count()>=syncChangeSeconds){try{output->syncAdjustment([self effectiveSyncSamples:changedSyncOffset]);}catch(const std::exception& e){std::cerr<<"FAIL live adjustment: "<<e.what()<<std::endl;[self quit];return;}prefs[@"syncOffsetMs"]=@(changedSyncOffset);syncChanged=true;std::cout<<"SYNC_ADJUSTMENT ms="<<changedSyncOffset<<" active_s="<<std::chrono::duration<double>(now-started).count()<<std::endl;}
  if(hardwareTest&&smokePhase==4&&(!status.running||!status.onAir)){std::cerr<<"FAIL hardware PLAY interrupted: "<<status.message<<"\n";[self quit];return;}
  // The bundled lower third (phases 6-9) needs its start, entry, exit and the next cycle of a
  // 10 s animation after the page loads: 25 s left about 3 s of margin, so it gets 45 s.
  if(std::chrono::steady_clock::now()-started>std::chrono::duration<double>((baselineSeconds>0&&(smokePhase==4||smokePhase==5))?baselineSeconds+60:(smokePhase>=6&&smokePhase<=9)?45:25)){std::cerr<<"FAIL smoke timeout phase "<<smokePhase<<" UI="<<ready<<" video="<<status.completed<<" audio="<<status.audioFrames<<" nonSilent="<<mixer->nonSilent<<" mixPts="<<mixer->lastMixTimestamp<<'\n';for(auto& r:runtimes)std::cerr<<"Source audio frames="<<r.second->audio->received<<" pts="<<r.second->audio->latestTimestamp<<" nonzero="<<r.second->audio->nonZero<<'\n';[self quit];return;}
  auto first=runtimes.find([(avLayerId?:layers.firstObject[@"id"]) UTF8String]);if(first==runtimes.end())return;auto frame=first->second->frame->get();
  if(referenceContentPts&&smokePhase==4&&(first->second->audio->referenceClock.origin==INT64_MIN||first->second->audio->referenceClock.mismatches)){std::cerr<<"FAIL identified audio content clock lost or changed\n";[self quit];return;}
  if(avLoopbackTest&&smokePhase==0&&frame&&ready&&(!referenceIds||(first->second->audio->referenceClock.origin!=INT64_MIN&&first->second->browser->lastReferenceId>=5))&&first->second->audio->received>4800&&first->second->browser->message=="Page loaded"){
   auto m=[self selectedMode];if(!m||m->id!=hardwareTestMode){std::cerr<<"FAIL loopback mode unavailable\n";[self quit];return;}
   try{[self startSDI:m];}catch(const std::exception& e){std::cerr<<"FAIL loopback output: "<<e.what()<<'\n';[self quit];return;}
   wasRunning=YES;smokePhase=4;started=std::chrono::steady_clock::now();std::cout<<"START SDI loopback source: "<<m->name<<std::endl;
  }
  else if(!avLoopbackTest&&smokePhase==0&&frame&&ready){
   if(uiSmoke<2){if(uiSmoke==0){uiSmoke=1;[web evaluateJavaScript:ns(R"JS((()=>{const q=s=>document.querySelector(s),assert=(x,m)=>{if(!x)throw Error(m)};document.querySelectorAll('.sr-drawer,.sr-drawer-inner').forEach(e=>e.style.transition='none');if(!q('.sr-device-select').options.length){assert(!q('.sr-driver-help').hidden,'Driver help missing');assert(q('[data-action=play]').disabled,'Play enabled without hardware');}const r=q('.sr-picture').getBoundingClientRect();assert(Math.abs(r.width/r.height-16/9)<.01,'Monitor image is not 16:9');assert(Math.abs(q('.sr-lcd').getBoundingClientRect().height-q('.sr-matrix').getBoundingClientRect().height)<1,'Monitor height differs from button matrix');assert(!q('[data-action="layers"]'),'Obsolete LAYERS button');assert(q('[data-action="add"]').disabled,'ADD LAYER enabled outside layer editing');q('[data-action="edit"]').click();assert(!q('[data-action="add"]').disabled,'ADD LAYER disabled while editing layers');q('.sr-layer-toggle').click();assert(q('.sr-layer-drawer').classList.contains('open'),'Layer drawer did not open');q('.sr-layer-toggle').click();assert(!q('.sr-layer-drawer').classList.contains('open'),'Layer drawer did not toggle closed');q('.sr-layer-toggle').click();q('[data-action="setup"]').click();assert(!q('.sr-layer-drawer').classList.contains('open')&&q('.sr-setup-drawer').classList.contains('open'),'Drawers not exclusive');const size=q('.sr-browser-size');size.value='custom';size.dispatchEvent(new Event('change'));assert(!q('.sr-custom-size').hidden,'Custom dimensions missing');size.value='1920x1080';size.dispatchEvent(new Event('change'));q('[data-action="close-setup"]').click();q('[data-action="edit"]').click();assert(q('[data-action="pause"]').disabled,'Pause allowed without Play');assert(q('.sr-matrix').children.length===8,'Wrong button matrix');assert(q('.sr-timing-profile').options.length===2&&q('.sr-sync-offset').min==='-200'&&q('.sr-sync-offset').max==='200','Timing controls missing');q('[data-action=audio]').click();assert(q('.sr-audio-drawer').classList.contains('open'),'Audio drawer did not open');assert(!q('.sr-layer-drawer').classList.contains('open'),'Audio and layer drawers overlap');assert(q('.sr-audio-title').textContent===q('.sr-layer-number').textContent+' · '+q('.sr-layer-name').textContent,'Audio title differs from layer');assert(q('.sr-audio-output .sr-audio-title').textContent==='PROGRAM','Missing audio output');assert(q('.sr-audio-lanes .sr-audio-pfl').textContent==='PFL MON.','Missing layer PFL');assert(q('.sr-audio-lanes .sr-audio-afl').textContent==='AFL MON.','Missing layer AFL');assert(q('.sr-audio-output .sr-audio-pfl')&&q('.sr-audio-output .sr-audio-afl'),'Missing program monitor modes');for(const strip of document.querySelectorAll('.sr-audio-strip')){const p=strip.querySelector('.sr-audio-pfl'),a=strip.querySelector('.sr-audio-afl'),pr=p.getBoundingClientRect(),ar=a.getBoundingClientRect();assert(Math.abs(pr.top-ar.top)<1&&ar.left>=pr.right,'Monitor buttons not side by side');p.dispatchEvent(new MouseEvent('mouseenter'));assert(q('.sr-monitor-tooltip').hidden,'Tooltip appeared without delay');p.dispatchEvent(new MouseEvent('mouseleave'));assert(p.dataset.monitorTooltip.startsWith('PFL MONITOR'+String.fromCharCode(10)),'PFL tooltip heading');}assert(!q('.sr-master-mute-button'),'Obsolete master mute button');q('[data-action=close-audio]').click();assert([...q('.sr-url-examples').options].some(o=>o.value==='u2k://Lower_Third/Animated_10_sec_cycle.html'),'Lower third missing from URL picker');assert(q('.sr-url-input').inputMode==='url'&&!q('.sr-url-input').readOnly,'Custom URL unavailable');assert(q('.sr-url-examples optgroup[label="Lower Third"] option').textContent==='Animated 10 sec cycle','Wrong lower-third catalog label');assert(q('.sr-url-examples optgroup[label="Transparency"] option').textContent==='Basic Test','Wrong transparency catalog label');assert(q('.sr-url-examples optgroup[label="Diagnostics"] option[value="u2k://Diagnostics/AV_Test.html"]'),'Automatic AV test missing');assert([...q('.sr-url-examples').options].filter(o=>o.value.startsWith('u2k://Diagnostics/')).length===12,'Incomplete AV diagnostics catalog');if(!q('.sr-layer-drawer').classList.contains('open')){q('[data-action=edit]').click();q('.sr-layer-toggle').click();}const picker=q('.sr-url-examples');picker.value='';picker.dispatchEvent(new Event('change'));assert(q('.sr-url-input').value==='https://','Custom URL prefix not reset');assert(document.activeElement===q('.sr-url-input')&&q('.sr-url-input').selectionStart===8&&q('.sr-url-input').selectionEnd===8,'Custom URL cursor not at end');picker.value='u2k://Transparency/Basic_Test.html';picker.dispatchEvent(new Event('change'));assert(!q('.sr-url-input').hidden&&q('.sr-url-input').value===picker.value,'Preset URL not visible');const pr=picker.getBoundingClientRect(),ur=q('.sr-url-input').getBoundingClientRect();assert(ur.left>=pr.right&&Math.abs(ur.top-pr.top)<1,'URL list and input are not side by side');return true;})())JS") completionHandler:^(id result,NSError* error){if(error||![result boolValue]){std::cerr<<"FAIL UI behavior: "<<(error?error.userInfo.description.UTF8String:"assertion")<<'\n';[self quit];}else{uiSmoke=2;std::cout<<"PASS monitor size, drawers, edit toggle, custom dimensions and pause guard\n";}}];}return;}
auto alpha=frame->pixels[80*frame->stride+110*4+3];if(alpha<120||alpha>135){std::cerr<<"FAIL browser alpha\n";[self quit];return;}std::cout<<"PASS native rack UI bridge and real transparent CEF rendering\n";auto l=[self newLayer];l[@"name"]=@"Second test";[layers insertObject:l atIndex:0];[self createRuntime:l];[self configureScene];smokePhase=1;}
  else if(smokePhase==1&&image&&image->size==u2k::Size{1920,1080}&&image->pixels[80*image->stride+110*4+3]>185){std::cout<<"PASS independent browser layers and premultiplied alpha composition\n";layers[0][@"visible"]=@NO;[self configureScene];smokePhase=2;}
  else if(smokePhase==2&&image&&image->pixels[80*image->stride+110*4+3]>=120&&image->pixels[80*image->stride+110*4+3]<=135){std::cout<<"PASS visibility changes composite without unloading browser\n";[self save];NSData* data=[NSData dataWithContentsOfFile:ns(settingsPath().string())];NSDictionary* saved=[NSJSONSerialization JSONObjectWithData:data options:0 error:nil];if([saved[@"layers"] count]!=2||![saved[@"preferences"][@"timingProfile"] isEqual:prefs[@"timingProfile"]]||![saved[@"preferences"][@"syncOffsetMs"] isEqual:prefs[@"syncOffsetMs"]]){[self quit];return;}std::cout<<"PASS persisted layer configuration\n";smokePhase=3;}
  else if(smokePhase==3){uint64_t audio=0;for(auto& r:runtimes)audio+=r.second->audio->received;if(audio>4800){std::cout<<"PASS CEF 48 kHz stereo audio capture: "<<audio<<" samples\n";if(monitorTest){monitorOutput=YES;[self configureAudio];[self startAudioMonitor];if(!audioEngine.isRunning){std::cerr<<"FAIL monitor start\n";[self quit];return;}[self audioConfigurationChanged];if(!audioEngine.isRunning){std::cerr<<"FAIL monitor rebuild\n";[self quit];return;}monitorOutput=NO;[self configureAudio];if(audioEngine){std::cerr<<"FAIL unused monitor engine not stopped\n";[self quit];return;}std::cout<<"PASS local monitor start, configuration rebuild and idle stop\n";}if(hardwareTest){auto m=[self selectedMode];if(!m||m->id!=hardwareTestMode){std::cerr<<"FAIL requested hardware test mode unavailable\n";[self quit];return;}std::cout<<"Hardware test mode: "<<m->name<<"\n";try{[self startSDI:m];}catch(const std::exception& e){std::cerr<<"FAIL SDI start: "<<e.what()<<"\n";[self quit];return;}wasRunning=YES;smokePhase=4;started=std::chrono::steady_clock::now();}else{auto example=[self newLayer];example[@"id"]=@"lowerthird-smoke";example[@"url"]=@"u2k://Lower_Third/Animated_10_sec_cycle.html";[self createRuntime:example];smokePhase=6;started=std::chrono::steady_clock::now();}}}
  else if(smokePhase==4&&status.completed>120&&status.audioFrames>48000&&mixer->nonSilent>4800&&(baselineSeconds<=0||std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count()>=baselineSeconds)){if(!status.audioClockCalibrated){std::cerr<<"FAIL SDI clock not calibrated\n";[self quit];return;}[self recordAudioTelemetry];output->onAir(false);if(!mixer->enabled){std::cerr<<"FAIL PAUSE changed shared mixer state\n";[self quit];return;}smokePhase=5;std::cout<<"PASS scheduled SDI video and stereo audio\n";}
  else if(smokePhase==5&&status.completed>150){output->onAir(true);output->onAir(false);[self stopSDI];if(!mixer->enabled||!output->status().onAir){std::cerr<<"FAIL PAUSE to STOP leaves shared mixer disabled\n";[self quit];return;}smokeResult=0;std::cout<<"PASS SDI pause, resume and stop restores shared mixer\n";[self quit];}
  else if(smokePhase>=6&&smokePhase<=9){
   auto example=runtimes.find("lowerthird-smoke");if(example==runtimes.end())return;auto sample=example->second->frame->get();if(!sample||example->second->browser->message!="Page loaded")return;
   bool visible=false;for(int y=0;y<sample->size.height&&!visible;y+=16)for(int x=0;x<sample->size.width;x+=16)if(sample->pixels[y*sample->stride+x*4+3]>20){visible=true;break;}
   if((smokePhase==6||smokePhase==8)?!visible:visible){++smokePhase;if(smokePhase==10){std::cout<<"PASS bundled lower third: transparent start, entry, exit and repeat in real CEF\n";auto av=[self newLayer];av[@"id"]=@"av-smoke";av[@"url"]=@"u2k://Diagnostics/AV_Test.html";[self createRuntime:av];started=std::chrono::steady_clock::now();}}
  }
  else if(smokePhase==10){
   auto av=runtimes.find("av-smoke");if(av==runtimes.end())return;
   auto sample=av->second->frame->get();if(!sample||av->second->browser->message!="Page loaded")return;
   bool rendered=sample->pixels[(sample->size.height/2)*sample->stride+(sample->size.width/8)*4+3]>240;
   if(rendered&&av->second->audio->nonZero>4800){smokeResult=0;std::cout<<"PASS automatic AV diagnostic: bundled catalog, opaque canvas and generated audio in real CEF without input\n";[self quit];}
  }
 }
}
-(void)requestQuit{if(!output||!output->status().running){[self quit];return;}if(ready){[web evaluateJavaScript:@"window.confirmQuit()" completionHandler:nil];}else{NSAlert* alert=[[NSAlert alloc]init];alert.alertStyle=NSAlertStyleCritical;alert.messageText=@"WARNING — SDI OUTPUT ACTIVE";alert.informativeText=@"Quitting will immediately cut the SDI video and audio signal. This may interrupt a live broadcast. Are you sure you want to stop output and quit?";[alert addButtonWithTitle:@"Cancel"];[alert addButtonWithTitle:@"Stop output and quit"];if([alert runModal]==NSAlertSecondButtonReturn)[self quit];}}
-(BOOL)windowShouldClose:(NSWindow*)sender{[self requestQuit];return NO;}
-(void)quit{if(closing)return;try{[self save];}catch(const std::exception& e){std::cerr<<e.what()<<'\n';}closing=YES;if(audioWakeObserver){[NSWorkspace.sharedWorkspace.notificationCenter removeObserver:audioWakeObserver];audioWakeObserver=nil;}[self recordAudioTelemetry];if(audioTrace.is_open())audioTrace.close();[self stopAudioMonitor];[self stopSDI];
 if(renderClock){renderClock->quit=true;if(renderClock->thread.joinable())renderClock->thread.join();auto& c=*renderClock;std::cerr<<"RENDER_CLOCK sent="<<c.sent<<" lead_ms="<<c.lead/48.<<" sdi_locked="<<c.locked<<" dispatch_mean_ms="<<(c.sent?double(c.sumDispatch)/c.sent/48.:0)<<" dispatch_min_ms="<<(c.sent?c.minDispatch/48.:0)<<" dispatch_max_ms="<<(c.sent?c.maxDispatch/48.:0)<<" dispatch_beyond_half_frame="<<c.lateDispatch;for(auto& r:runtimes)std::cerr<<" layer="<<r.first<<" begin_frames="<<r.second->browser->beginFrames<<" paint_delay_max_ms="<<r.second->browser->paintDelayMax/48.<<" paints_beyond_one_frame="<<r.second->browser->paintsLate<<" unpainted_begin_frames="<<r.second->browser->unpaintedBeginFrames<<" unpainted_after_short_gap="<<r.second->browser->unpaintedAfterShortGap<<" extra_paints="<<r.second->browser->extraPaints<<" min_begin_frame_gap_ms="<<r.second->browser->minBeginFrameGap/48.;const auto status=output->status();std::cerr<<" video_repeats="<<status.videoRepeats<<" video_skips="<<status.videoSkips<<std::endl;}[web.configuration.userContentController removeScriptMessageHandlerForName:@"native"];for(auto& r:runtimes)r.second->browser->close();for(auto& r:retired)r->browser->close();}
-(BOOL)applicationSupportsSecureRestorableState:(NSApplication*)app{return YES;}
@end
int main(int argc,char** argv){
 for(int i=1;i<argc;i++){if(std::string(argv[i])=="--sdi-diagnostic-stop-file"){if(i+1>=argc)return 2;diagnosticStopFile=argv[++i];}if(std::string(argv[i])=="--sdi-sync-calibration-file"){if(i+1>=argc)return 2;syncCalibrationFile=argv[++i];}if(std::string(argv[i])=="--sync-change-ms"){if(i+1>=argc)return 2;char* end=nullptr;const long n=std::strtol(argv[++i],&end,10);if(*end||n< -200||n>200)return 2;changedSyncOffset=int(n);}if(std::string(argv[i])=="--timing-profile"){if(i+1>=argc)return 2;requestedTimingProfile=argv[++i];if(requestedTimingProfile!="reliable"&&requestedTimingProfile!="low")return 2;}if(std::string(argv[i])=="--sync-offset-ms"){if(i+1>=argc)return 2;char* end=nullptr;long n=std::strtol(argv[++i],&end,10);if(*end||n< -200||n>200)return 2;requestedSyncOffset=int(n);}if(std::string(argv[i])=="--sdi-av-reference-ids")referenceIds=true;if(std::string(argv[i])=="--sdi-av-content-pts")referenceContentPts=true;if(std::string(argv[i])=="--sdi-av-source-url"){if(i+1>=argc)return 2;avTestUrl=argv[++i];if(!allowedUrl(avTestUrl))return 2;}if(std::string(argv[i])=="--sdi-av-overlay-url"){if(i+1>=argc)return 2;avOverlayUrl=argv[++i];if(!allowedUrl(avOverlayUrl))return 2;}if(std::string(argv[i])=="--sdi-av-test"){smoke=true;hardwareTest=true;avLoopbackTest=true;if(baselineSeconds<=0)baselineSeconds=30;}if(std::string(argv[i])=="--hardware-test-mode"){if(i+1>=argc)return 2;const std::string mode=argv[++i];if(mode=="1080p25")hardwareTestMode=0x48703235;else if(mode=="1080p50")hardwareTestMode=0x48703530;else if(mode=="1080p5994")hardwareTestMode=0x48703539;else return 2;smoke=true;hardwareTest=true;}if(std::string(argv[i])=="--audio-baseline-seconds"){if(i+1>=argc)return 2;char* end=nullptr;baselineSeconds=std::strtod(argv[++i],&end);if(*end||!std::isfinite(baselineSeconds)||baselineSeconds<1||baselineSeconds>28860)return 2;smoke=true;hardwareTest=true;}if(std::string(argv[i])=="--monitor-smoke-test"){smoke=true;monitorTest=true;}if(std::string(argv[i])=="--smoke-test")smoke=true;if(std::string(argv[i])=="--no-av-calibration")rawAV=true;if(std::string(argv[i])=="--cef-rate-multiplier"){if(i+1>=argc)return 2;const std::string n=argv[++i];if(n!="1"&&n!="2")return 2;cefRateOverride=std::stoll(n);}if(std::string(argv[i])=="--external-begin-frame")externalBeginFrame=true;if(std::string(argv[i])=="--begin-frame-probe")beginFrameProbe=true;if(std::string(argv[i])=="--smoke-no-device-test"){smoke=true;noDeviceTest=true;}if(std::string(argv[i])=="--hardware-smoke-test"){smoke=true;hardwareTest=true;}}
 if((!requestedTimingProfile.empty()||requestedSyncOffset!=INT_MAX||changedSyncOffset!=INT_MAX||rawAV)&&!hardwareTest)return 2;
 if(!syncCalibrationFile.empty()&&(!avLoopbackTest||changedSyncOffset!=INT_MAX))return 2;
 if(!diagnosticStopFile.empty()&&!avLoopbackTest)return 2;
 if((!avTestUrl.empty()||!avOverlayUrl.empty())&&!avLoopbackTest)return 2;
 if(beginFrameProbe&&!externalBeginFrame)return 2;
 if((referenceIds&&(!avLoopbackTest||avTestUrl.empty()))||(referenceContentPts&&!referenceIds))return 2;
 if(const char* value=std::getenv("U2K_AUDIO_SOURCE_AGE_MS")){explicitSourceAge=true;char* end=nullptr;const double ms=std::strtod(value,&end);if(end==value||*end||!std::isfinite(ms)||ms<10||ms>200){std::cerr<<"U2K_AUDIO_SOURCE_AGE_MS must be 10–200 ms\n";return 2;}sourceAgeSamples=uint32_t(std::llround(ms*48));}
 CefScopedLibraryLoader loader;if(!loader.LoadInMain())return 1;
 @autoreleasepool{[U2KApplication sharedApplication];[NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];NSString* iconPath=[NSBundle.mainBundle pathForResource:@"URL2KeyFill" ofType:@"icns"];if(iconPath){NSImage* icon=[[NSImage alloc]initWithContentsOfFile:iconPath];if(icon)NSApp.applicationIconImage=icon;}AppDelegate* delegate=[[AppDelegate alloc]init];NSApp.delegate=delegate;CefRefPtr<BrowserApp> app=new BrowserApp;app->ready=[delegate]{[delegate setup];};
 CefSettings config;config.windowless_rendering_enabled=true;config.no_sandbox=false;config.command_line_args_disabled=true;auto cache=std::filesystem::temp_directory_path()/("url2keyfill-cef-"+std::string(NSUUID.UUID.UUIDString.UTF8String));CefString(&config.root_cache_path)=cache.string();CefString(&config.log_file)=(cache/"cef.log").string();CefMainArgs args(argc,argv);if(!CefInitialize(args,config,app,nullptr))return CefGetExitCode();
 CefRunMessageLoop();app->ready=nullptr;NSApp.delegate=nil;delegate=nil;app=nullptr;CefShutdown();std::error_code error;std::filesystem::remove_all(cache,error);}
 if(smoke)std::cout<<"CEF shutdown complete\n";return smoke?smokeResult:0;
}
