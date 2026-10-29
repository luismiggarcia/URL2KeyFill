#include "include/cef_app.h"
#include "include/cef_sandbox_mac.h"
#include "include/wrapper/cef_library_loader.h"
#include "browser/Browser.hpp"
int main(int argc,char** argv){
    CefScopedSandboxContext sandbox;
    if(!sandbox.Initialize(argc,argv))return 1;
    CefScopedLibraryLoader loader;if(!loader.LoadInHelper())return 1;
    CefMainArgs args(argc,argv);
    return CefExecuteProcess(args,new u2k::BrowserApp,nullptr);
}
