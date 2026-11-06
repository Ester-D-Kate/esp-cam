#include <cassert>
#include <cstdint>
#include <cstdio>
#include "Arduino.h"
#include "esp_timer.h"
#include "button_input.h"
TestSerial Serial;
static int create_result=ESP_FAIL,start_result=ESP_OK,deletes=0,pin=-1,mode=-1,physical=HIGH;
static int64_t now=0;
static esp_timer_cb_t callback=nullptr;
int64_t esp_timer_get_time(){return now;}
uint32_t esp_random(){static uint32_t id=40;return ++id;}
void pinMode(int p,int m){pin=p;mode=m;}
int digitalRead(int p){assert(p==12);return physical;}
esp_err_t esp_timer_create(const esp_timer_create_args_t* a,esp_timer_handle_t* h){callback=a->callback;assert(a->dispatch_method==ESP_TIMER_TASK);*h=create_result==ESP_OK?reinterpret_cast<void*>(1):nullptr;return create_result;}
esp_err_t esp_timer_start_periodic(esp_timer_handle_t,uint64_t period){assert(period==5000);return start_result;}
esp_err_t esp_timer_delete(esp_timer_handle_t){++deletes;return ESP_OK;}
int main(){
 usb_cam::Header h;
 assert(!button_input::begin());button_input::write_header(h);assert(!(h.capabilities & usb_cam::kButtonCapability));
 create_result=ESP_OK;start_result=ESP_FAIL;
 assert(!button_input::begin());assert(deletes==1);
 start_result=ESP_OK;assert(button_input::begin());assert(pin==12 && mode==INPUT_PULLUP);
 physical=LOW;now=100000;callback(nullptr);now=149000;callback(nullptr);
 button_input::write_header(h);assert(!h.button.pressed && h.button.sequence==0);
 now=150000;callback(nullptr);button_input::write_header(h);assert(h.button.pressed && h.button.sequence==1);
 const uint32_t old=h.button.boot_id;
 button_input::acknowledge({old,usb_cam::kButtonResync});button_input::write_header(h);
 assert(h.button.boot_id!=old && h.button.sequence==0 && h.button.pressed);
 physical=HIGH;now=200000;callback(nullptr);now=250000;callback(nullptr);button_input::write_header(h);
 assert(!h.button.pressed && h.button.sequence==1);
 puts("ESP GPIO12 pull-up timer, failure cleanup and independent edge sampling passed.");
}
