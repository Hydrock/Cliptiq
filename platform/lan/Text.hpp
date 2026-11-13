#pragma once
#include <algorithm>
#include <string>
namespace cliptiq::lan {
inline bool valid_device_id(const std::string& s){
 return !s.empty()&&s.size()<=128&&std::all_of(s.begin(),s.end(),[](unsigned char c){
  return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_';
 });
}
// Strict UTF-8 and visible labels; reject control/bidi-control code points.
inline bool valid_display_text(const std::string& s,size_t limit,bool allow_empty=false){
 if((s.empty()&&!allow_empty)||s.size()>limit)return false;
 for(size_t at=0;at<s.size();){
  unsigned c=static_cast<unsigned char>(s[at++]),n=0,min=0;
  if(c>=0xc2&&c<=0xdf){c&=31;n=1;min=0x80;}
  else if(c>=0xe0&&c<=0xef){c&=15;n=2;min=0x800;}
  else if(c>=0xf0&&c<=0xf4){c&=7;n=3;min=0x10000;}
  else if(c>=0x80)return false;
  if(n>s.size()-at)return false;
  while(n--){auto d=static_cast<unsigned char>(s[at++]);if((d&0xc0)!=0x80)return false;c=(c<<6)|(d&63);}
  if(c<min||c>0x10ffff||(c>=0xd800&&c<=0xdfff)||c<32||(c>=0x7f&&c<=0x9f)||
     (c>=0x202a&&c<=0x202e)||(c>=0x2066&&c<=0x2069))return false;
 }
 return true;
}
}
