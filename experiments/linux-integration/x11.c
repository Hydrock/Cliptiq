#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/extensions/XTest.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(void) {
 Display *a=XOpenDisplay(NULL), *b=XOpenDisplay(NULL); if(!a||!b)return 2;
 Window owner=XCreateSimpleWindow(a,DefaultRootWindow(a),0,0,10,10,0,0,0);
 Window reader=XCreateSimpleWindow(b,DefaultRootWindow(b),0,0,10,10,0,0,0);
 Atom sel=XInternAtom(a,"CLIPBOARD",False), utf=XInternAtom(a,"UTF8_STRING",False), prop=XInternAtom(a,"CLIPTIQ_TEST",False);
 const char *fixture="Cliptiq isolated UTF-8: Привет";
 XSetSelectionOwner(a,sel,owner,CurrentTime); XSync(a,False);
 if(XGetSelectionOwner(b,sel)!=owner)return 3;
 XConvertSelection(b,sel,utf,prop,reader,CurrentTime); XFlush(b);
 struct pollfd f={ConnectionNumber(a),POLLIN,0}; if(poll(&f,1,3000)<=0)return 4;
 XEvent e; XNextEvent(a,&e); if(e.type!=SelectionRequest)return 5;
 XSelectionRequestEvent *r=&e.xselectionrequest;
 XChangeProperty(a,r->requestor,r->property,utf,8,PropModeReplace,(unsigned char*)fixture,strlen(fixture));
 XEvent reply={0}; reply.xselection=(XSelectionEvent){.type=SelectionNotify,.display=a,.requestor=r->requestor,.selection=sel,.target=utf,.property=prop,.time=r->time};
 XSendEvent(a,r->requestor,False,0,&reply); XFlush(a);
 f.fd=ConnectionNumber(b); if(poll(&f,1,3000)<=0)return 6; XNextEvent(b,&e); if(e.type!=SelectionNotify)return 7;
 Atom actual; int format; unsigned long n,left; unsigned char *data;
 XGetWindowProperty(b,reader,prop,0,1024,False,AnyPropertyType,&actual,&format,&n,&left,&data);
 if(n!=strlen(fixture)||memcmp(data,fixture,n))return 8;
 XFree(data);
 KeyCode key=XKeysymToKeycode(a,XK_F12),ctrl=XKeysymToKeycode(b,XK_Control_L),shift=XKeysymToKeycode(b,XK_Shift_L);
 XMapWindow(b,reader); XSync(b,False); XSetInputFocus(b,reader,RevertToParent,CurrentTime); XSync(b,False);
 XGrabKey(a,key,ControlMask|ShiftMask,DefaultRootWindow(a),False,GrabModeAsync,GrabModeAsync); XSync(a,False);
 XTestFakeKeyEvent(b,ctrl,True,0); XTestFakeKeyEvent(b,shift,True,0); XTestFakeKeyEvent(b,key,True,0); XTestFakeKeyEvent(b,key,False,0); XTestFakeKeyEvent(b,shift,False,0); XTestFakeKeyEvent(b,ctrl,False,0); XSync(b,False);
 f.fd=ConnectionNumber(a);
 for(int attempt=0;attempt<30;attempt++) {
  if(!XPending(a)&&poll(&f,1,100)<=0)continue;
  XNextEvent(a,&e);
  if(e.type==KeyPress&&e.xkey.keycode==key)goto hotkey_ok;
 }
 return 10;
 hotkey_ok:
 puts("{\"selection_owner\":true,\"utf8_roundtrip\":true,\"global_grab_hotkey\":true,\"input\":\"XTest synthetic Ctrl+Shift+F12\"}");
 XCloseDisplay(b); XCloseDisplay(a); return 0;
}
