#include "platform/windows/Sound.hpp"
#include <windows.h>
#include <mmsystem.h>
#include <iostream>
#include <stdexcept>
static unsigned calls=0,last_flags=0;
extern "C" BOOL WINAPI cliptiq_test_play_sound(LPCWSTR bytes, HMODULE, DWORD flags) {
  if(!bytes)return TRUE;
  ++calls;last_flags=flags;
  return TRUE;
}
#define CHECK(x) do{if(!(x))throw std::runtime_error(#x);}while(false)
int main(){try{
  cliptiq::windows::NativeSound sound;
  sound.configure(true,true,25);
  sound.play_copy();sound.play_copy();sound.play_paste();
  CHECK(calls==3); // Every confirmed event reaches audio, including bursts.
  CHECK((last_flags&SND_NOSTOP)==0);
  CHECK((last_flags&(SND_ASYNC|SND_MEMORY|SND_NODEFAULT))==(SND_ASYNC|SND_MEMORY|SND_NODEFAULT));
  sound.configure(false,false,25);sound.play_copy();sound.play_paste();CHECK(calls==3);
  sound.configure(true,true,0);sound.play_copy();CHECK(calls==3);
  sound.preview(false,25);CHECK(calls==4);
  std::cout<<"PASS rapid copy/paste cues, playback flags, toggles and mute\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
