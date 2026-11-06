#include "../NFC_Toolkit/ndef_builder.h"
#include <cassert>
static void show(const char*name,uint8_t*b,size_t n){printf("%s (%zu): %s\n%s\n",name,n,toHex(b,n).c_str(),ndefDescribeMessage(b,n).c_str());}
int main(){
  uint8_t buf[512]; NdefBuilder nb(buf,sizeof buf);
  nb.reset(); nb.addUri("https://www.st.com"); size_t n=nb.finish(); show("uri",buf,n);
  const uint8_t expect[]={0xD1,0x01,0x07,0x55,0x02,'s','t','.','c','o','m'}; assert(n==sizeof expect && !memcmp(buf,expect,n));
  nb.reset(); nb.addText("Hello"); n=nb.finish(); show("text",buf,n);
  const uint8_t et[]={0xD1,0x01,0x08,'T',0x02,'e','n','H','e','l','l','o'}; assert(n==sizeof et && !memcmp(buf,et,n));
  nb.reset(); nb.addWifi("MyNet","secret123",WSC_AUTH_WPA2_PSK,WSC_ENC_AES); n=nb.finish(); show("wifi",buf,n);
  assert(buf[0]==0xD2);
  nb.reset(); nb.addUri("tel:+15551234"); nb.addText("two"); nb.addAndroidApp("com.x"); n=nb.finish(); show("multi",buf,n);
  assert(buf[0]==0x91); // MB, SR, WK, no ME
  std::string big(300,'a'); nb.reset(); nb.addText(big.c_str()); n=nb.finish(); assert(buf[0]==(0x80|0x40|0x01)); printf("long record hdr ok, len %zu\n",n);
  uint8_t empty[]={0xD0,0,0}; show("empty",empty,3);
  uint8_t small[8]; NdefBuilder of(small,sizeof small); of.reset(); of.addText("overflowing text"); assert(of.finish()==0 && of.overflow());
  puts("ALL OK");
}
