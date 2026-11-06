#include <cassert>
#include <cstdio>
#include "usb_cam/button_hid.h"
using namespace usb_cam;
static void synchronize(ButtonBridge& b,uint32_t old_session=1,uint32_t new_session=2,bool pressed=false){
 b.observe(true,{old_session,8,90,pressed},100);
 assert(b.ack().sequence==kButtonResync && b.ack().boot_id==old_session);
 b.observe(true,{old_session,9,95,!pressed},105);assert(!(b.snapshot().flags & kButtonAvailable));
 b.observe(true,{new_session,0,110,pressed},110);
 assert((b.snapshot().flags & (kButtonAvailable|kButtonSync))==(kButtonAvailable|kButtonSync));
 assert(b.ack().boot_id==new_session && b.ack().sequence==0);
}
static void clear(ButtonBridge& b){ButtonReport r;while(b.peek(r))assert(b.commit(r));}
int main(){
 ButtonBridge b; assert(!(b.snapshot().flags & kButtonAvailable));
 synchronize(b);clear(b);
 b.observe(true,{2,1,200,true},200);
 ButtonReport press;assert(b.peek(press));assert(press.flags== (kButtonAvailable|kButtonPressed));
 auto revision=press.revision;
 b.observe(true,{2,1,200,true},210);assert(b.pending()==1);
 b.observe(true,{2,2,260,false},260);assert(b.pending()==2);
 assert(b.commit(press));ButtonReport release;assert(b.peek(release));
 assert(release.flags==kButtonAvailable && release.event.timestamp_ms==260);
 assert(!b.commit(press));assert(b.commit(release));
 b.expire(2259);assert(b.snapshot().flags & kButtonAvailable);
 b.expire(2260);assert(!(b.snapshot().flags & kButtonAvailable));
 assert(b.snapshot().revision!=revision && (b.snapshot().flags & kButtonSync));
 auto expired=b.snapshot().revision;b.expire(2300);assert(b.snapshot().revision==expired);
 // Reconnection has an old press/release backlog. No edge may leak before fresh ID.
 b.observe(true,{2,3,270,true},2400);assert(!(b.snapshot().flags & kButtonAvailable));
 b.observe(true,{2,4,320,false},2401);assert(!(b.snapshot().flags & kButtonAvailable));
 b.observe(true,{3,0,2402,false},2402);assert(b.snapshot().flags==(kButtonAvailable|kButtonSync));
 clear(b);b.observe(true,{3,1,2500,true},2500);assert(b.snapshot().flags==(kButtonAvailable|kButtonPressed));
 // A user resync while held reports held state only, never a synthetic press.
 b.request_resync();assert(b.ack().boot_id==3 && b.ack().sequence==kButtonResync);
 b.observe(true,{4,0,2600,true},2600);assert(b.snapshot().flags==(kButtonAvailable|kButtonSync|kButtonPressed));
 clear(b);
 // Missing sequence invalidates gestures and requires fresh snapshot.
 b.observe(true,{4,3,2700,false},2700);assert(b.ack().sequence==kButtonResync);
 assert((b.snapshot().flags & (kButtonSync|kButtonLoss))==(kButtonSync|kButtonLoss));
 b.observe(true,{5,0,2800,false},2800);
 assert(b.snapshot().flags & kButtonLoss); // Loss survives if its earlier report was not yet consumed.
 clear(b);
 // Unread HID fills only its own FIFO; bounded loss forces resync, never waits.
 for(uint32_t i=1;i<=33;++i)b.observe(true,{5,i,2800+i,bool(i&1)},2800+i);
 assert(b.pending()<=32 && b.ack().sequence==kButtonResync);
 assert(b.snapshot().flags & kButtonLoss);
 b.observe(false,{},3000);assert(!b.ack().boot_id && !(b.snapshot().flags & kButtonAvailable));
 uint8_t bytes[kButtonReportBytes];ButtonReport r{{7,0x7fffffffu,0xfffffff0u,true},uint8_t(kButtonAvailable|kButtonPressed),65535};
 encode_button_report(r,bytes);assert(bytes[0]==1 && bytes[1]==3 && get32(bytes+2)==0x7fffffffu && get32(bytes+10)==7 && get16(bytes+14)==65535);
 // A second reader request can arrive after ESP applied A->B but before Pico saw B.
 // B's offline edges must stay hidden; request B->C before exposing state.
 ButtonBridge superseded;synchronize(superseded);clear(superseded);
 superseded.request_resync(); // ESP will change 2 -> 3.
 superseded.request_resync(); // A newer reader needs state after this request.
 superseded.observe(true,{3,0,3100,false},3100);
 assert(!(superseded.snapshot().flags & kButtonAvailable));
 assert(superseded.ack().boot_id==3 && superseded.ack().sequence==kButtonResync);
 superseded.observe(true,{3,1,3110,true},3110);
 superseded.observe(true,{3,2,3120,false},3120);
 assert(!(superseded.snapshot().flags & kButtonAvailable));
 superseded.observe(true,{4,0,3130,false},3130);
 assert(superseded.snapshot().flags==(kButtonAvailable|kButtonSync));
 clear(superseded);superseded.observe(true,{4,1,3200,true},3200);
 assert(superseded.snapshot().flags==(kButtonAvailable|kButtonPressed));
 // Repeated lifecycle changes retain their ordering across report revision wrap.
 ButtonBridge wrap;uint16_t previous=wrap.snapshot().revision;
 for(unsigned i=0;i<65540;++i){wrap.request_resync();auto current=wrap.snapshot().revision;assert(uint16_t(current-previous)==1);previous=current;}
 puts("HID press/release FIFO, dedup, fresh resync, loss, timeout and revision wrap passed.");
}
