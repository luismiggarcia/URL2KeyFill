#include "Browser.hpp"
#include "include/wrapper/cef_stream_resource_handler.h"
#include "include/cef_stream.h"
#include <stdexcept>
namespace u2k {
void Browser::create(const std::string& url){
    if(!allowedUrl(url))throw std::invalid_argument("Unsupported URL.");
    CefWindowInfo wi;wi.SetAsWindowless(0);CefBrowserSettings s;
    s.background_color=CefColorSetARGB(0,0,0,0);s.windowless_frame_rate=60;
    if(!CefBrowserHost::CreateBrowser(wi,this,url,s,nullptr,nullptr))throw std::runtime_error("Unable to create the Chromium browser.");
}
void Browser::load(const std::string& url,Size size){
    validate(size);if(!allowedUrl(url))throw std::invalid_argument("Unsupported URL.");
    if(!browser_){size_=size;pendingUrl_=url;return;}
    size_=size;failed_=false;frames_.clear();audio_->clear();browser_->GetHost()->WasResized();
    if(browser_->GetMainFrame()->GetURL()==url)browser_->ReloadIgnoreCache();else browser_->GetMainFrame()->LoadURL(url);
}
void Browser::close(){closing_=true;if(browser_)browser_->GetHost()->CloseBrowser(true);}
void Browser::OnPaint(CefRefPtr<CefBrowser>,PaintElementType type,const RectList&,const void* p,int w,int h){
    if(type!=PET_VIEW||failed_||w!=size_.width||h!=size_.height)return;
    try{frames_.publish(Frame::copy({w,h},p,size_t(w)*4,++sequence_));}catch(const std::exception& e){message=e.what();frames_.clear();audio_->clear();}
}
void Browser::OnLoadingStateChange(CefRefPtr<CefBrowser>,bool loading,bool,bool){if(loading){failed_=false;message="Loading…";}else if(!failed_)message="Page loaded";}
void Browser::OnLoadError(CefRefPtr<CefBrowser>,CefRefPtr<CefFrame> f,ErrorCode code,const CefString& text,const CefString&){if(!f->IsMain()||code==ERR_ABORTED)return;failed_=true;frames_.clear();audio_->clear();message="Load error: "+text.ToString();}
void Browser::OnLoadEnd(CefRefPtr<CefBrowser>,CefRefPtr<CefFrame> f,int status){if(f->IsMain()&&status>=400){failed_=true;frames_.clear();audio_->clear();message="HTTP error "+std::to_string(status)+" — layer cleared.";}}
void Browser::OnRenderProcessTerminated(CefRefPtr<CefBrowser>,TerminationStatus,int,const CefString& reason){failed_=true;frames_.clear();audio_->clear();message="Browser terminated: "+reason.ToString()+". Layer cleared; reload to recover.";}
// Only exact URLs discovered in the bundled assets catalog are served.
class ExampleFactory final:public CefSchemeHandlerFactory {
    std::map<std::string,std::string> pages_;
public:
    explicit ExampleFactory(std::map<std::string,std::string> pages):pages_(std::move(pages)){}
    CefRefPtr<CefResourceHandler> Create(CefRefPtr<CefBrowser>,CefRefPtr<CefFrame>,const CefString&,CefRefPtr<CefRequest> r)override{
        auto page=pages_.find(canonicalBuiltinUrl(r->GetURL().ToString()));if(page==pages_.end())return nullptr;
        auto stream=CefStreamReader::CreateForData(page->second.data(),page->second.size());return new CefStreamResourceHandler("text/html",stream);
    }
    IMPLEMENT_REFCOUNTING(ExampleFactory);
};
void registerExamplePages(std::map<std::string,std::string> pages){
    CefRefPtr<ExampleFactory> factory=new ExampleFactory(std::move(pages));
    CefRegisterSchemeHandlerFactory("u2k","",factory);
}
}
