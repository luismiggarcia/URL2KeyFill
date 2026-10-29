#include "Pipeline.hpp"
namespace u2k {
Pipeline::Pipeline():worker_([this]{run();}){}
Pipeline::~Pipeline(){{std::lock_guard<std::mutex> l(mutex_);quit_=true;}cv_.notify_one();worker_.join();}
void Pipeline::submit(std::shared_ptr<const Frame> f) {std::lock_guard<std::mutex> l(mutex_);raw_.publish(std::move(f));++received_;dirty_=true;cv_.notify_one();}
void Pipeline::target(Size s){validate(s);std::lock_guard<std::mutex> l(mutex_);target_=s;dirty_=true;cv_.notify_one();}
void Pipeline::clear(){std::lock_guard<std::mutex> l(mutex_);raw_.clear();fitted_.clear();dirty_=true;cv_.notify_one();}
void Pipeline::run(){
    for(;;){std::unique_lock<std::mutex> l(mutex_);cv_.wait(l,[this]{return dirty_||quit_;});if(quit_)return;
        auto src=raw_.get();auto size=target_;dirty_=false;l.unlock();
        std::shared_ptr<const Frame> next;
        try {if(src)next=src->size==size?src:resize(*src,size);}catch(...){next=nullptr;}
        l.lock();if(!dirty_){fitted_.publish(std::move(next));++processed_;}
    }
}
}
