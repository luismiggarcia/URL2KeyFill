#pragma once
#include "core/Scene.hpp"
#include "include/cef_audio_handler.h"
#include "include/cef_permission_handler.h"
#include "include/cef_app.h"
#include "include/cef_client.h"
#include "include/cef_parser.h"
#include "include/cef_scheme.h"
#include <functional>
#include <chrono>
#include <cstdlib>
namespace u2k {
class Browser final: public CefClient, public CefRenderHandler, public CefLifeSpanHandler, public CefLoadHandler, public CefRequestHandler, public CefAudioHandler, public CefPermissionHandler {
    LatestFrame& frames_;
    std::shared_ptr<AudioLane> audio_;
    std::atomic<int> channels_{2};
    int64_t audioOffset_=0;bool audioClockReady_=false;
    std::string pendingUrl_;
    Size size_;
    CefRefPtr<CefBrowser> browser_;
    uint64_t sequence_=0;
    bool failed_=false, closing_=false;
public:
    std::string message="Starting browser…";
    std::function<void()> closed;
    Browser(LatestFrame& f,Size s,std::shared_ptr<AudioLane> audio):frames_(f),audio_(std::move(audio)),size_(s){}
    CefRefPtr<CefPermissionHandler> GetPermissionHandler()override{return this;}
    bool OnRequestMediaAccessPermission(CefRefPtr<CefBrowser>,CefRefPtr<CefFrame>,const CefString&,uint32_t,CefRefPtr<CefMediaAccessCallback> callback)override{callback->Continue(CEF_MEDIA_PERMISSION_NONE);return true;}
    bool OnShowPermissionPrompt(CefRefPtr<CefBrowser>,uint64_t,const CefString&,uint32_t,CefRefPtr<CefPermissionPromptCallback> callback)override{callback->Continue(CEF_PERMISSION_RESULT_DENY);return true;}
    bool ready()const{return !!browser_;}
    uint64_t paints()const{return sequence_;}
    CefRefPtr<CefAudioHandler> GetAudioHandler()override{return this;}
    bool GetAudioParameters(CefRefPtr<CefBrowser>,CefAudioParameters& p)override{p.sample_rate=48000;p.channel_layout=CEF_CHANNEL_LAYOUT_STEREO;p.frames_per_buffer=480;return true;}
    void OnAudioStreamStarted(CefRefPtr<CefBrowser>,const CefAudioParameters&,int channels)override{channels_=channels;audioClockReady_=false;audio_->clear();}
    void OnAudioStreamPacket(CefRefPtr<CefBrowser>,const float** data,int frames,int64_t pts)override{if(!audioClockReady_){const auto now=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();audioOffset_=std::abs(now-pts)>60000?now-pts:0;audioClockReady_=true;}audio_->push(data,frames,channels_.load(),pts+audioOffset_);}
    void OnAudioStreamStopped(CefRefPtr<CefBrowser>)override{audio_->clear();}
    void OnAudioStreamError(CefRefPtr<CefBrowser>,const CefString&)override{audio_->clear();}
    void create(const std::string&);
    void load(const std::string&,Size);
    void close();
    CefRefPtr<CefRenderHandler> GetRenderHandler()override{return this;}
    CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler()override{return this;}
    CefRefPtr<CefLoadHandler> GetLoadHandler()override{return this;}
    CefRefPtr<CefRequestHandler> GetRequestHandler()override{return this;}
    void GetViewRect(CefRefPtr<CefBrowser>,CefRect& r)override{r=CefRect(0,0,size_.width,size_.height);}
    bool GetScreenInfo(CefRefPtr<CefBrowser>,CefScreenInfo& s)override{s.device_scale_factor=1;s.depth=32;s.depth_per_component=8;s.rect=s.available_rect=CefRect(0,0,size_.width,size_.height);return true;}
    void OnPaint(CefRefPtr<CefBrowser>,PaintElementType,const RectList&,const void*,int,int)override;
    void OnAfterCreated(CefRefPtr<CefBrowser> b)override{browser_=b;if(!pendingUrl_.empty()){b->GetMainFrame()->LoadURL(pendingUrl_);pendingUrl_.clear();}if(closing_)b->GetHost()->CloseBrowser(true);}
    void OnBeforeClose(CefRefPtr<CefBrowser>)override{browser_=nullptr;if(closed)closed();}
    bool OnBeforePopup(CefRefPtr<CefBrowser>,CefRefPtr<CefFrame>,int,const CefString&,const CefString&,WindowOpenDisposition,bool,const CefPopupFeatures&,CefWindowInfo&,CefRefPtr<CefClient>&,CefBrowserSettings&,CefRefPtr<CefDictionaryValue>&,bool*)override{return true;}
    bool OnBeforeBrowse(CefRefPtr<CefBrowser>,CefRefPtr<CefFrame>,CefRefPtr<CefRequest> r,bool,bool)override{return !allowedUrl(r->GetURL().ToString());}
    void OnLoadingStateChange(CefRefPtr<CefBrowser>,bool,bool,bool)override;
    void OnLoadError(CefRefPtr<CefBrowser>,CefRefPtr<CefFrame>,ErrorCode,const CefString&,const CefString&)override;
    void OnLoadEnd(CefRefPtr<CefBrowser>,CefRefPtr<CefFrame>,int)override;
    void OnRenderProcessTerminated(CefRefPtr<CefBrowser>,TerminationStatus,int,const CefString&)override;
    IMPLEMENT_REFCOUNTING(Browser);
};
class BrowserApp final:public CefApp,public CefBrowserProcessHandler {
public:
    std::function<void()> ready;
    CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler()override{return this;}
    void OnContextInitialized()override{if(ready)ready();}
    void OnRegisterCustomSchemes(CefRawPtr<CefSchemeRegistrar> r)override{r->AddCustomScheme("u2k",CEF_SCHEME_OPTION_STANDARD|CEF_SCHEME_OPTION_SECURE);}
    void OnBeforeCommandLineProcessing(const CefString&,CefRefPtr<CefCommandLine> c)override{c->AppendSwitchWithValue("force-color-profile","srgb");c->AppendSwitchWithValue("autoplay-policy","no-user-gesture-required");
#ifdef __APPLE__
        // Dedicated, non-persistent application profile; never use the user keychain.
        c->AppendSwitch("use-mock-keychain");
#endif
}
    IMPLEMENT_REFCOUNTING(BrowserApp);
};
void registerTestPage(std::string html);
}
