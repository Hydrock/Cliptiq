#import <AppKit/AppKit.h>
#import <Vision/Vision.h>
#include "ocr.h"
const char* native_ocr_test() {
    @autoreleasepool {
        NSImage* image=[[NSImage alloc] initWithSize:NSMakeSize(400,80)];
        [image lockFocus]; [[NSColor whiteColor] setFill]; NSRectFill(NSMakeRect(0,0,400,80));
        [@"Cliptiq" drawAtPoint:NSMakePoint(10,20) withAttributes:@{NSFontAttributeName:[NSFont systemFontOfSize:40],NSForegroundColorAttributeName:[NSColor blackColor]}];
        [image unlockFocus]; CGImageRef cg=[image CGImageForProposedRect:nullptr context:nil hints:nil];
        VNRecognizeTextRequest* request=[VNRecognizeTextRequest new]; request.recognitionLevel=VNRequestTextRecognitionLevelAccurate;
        request.recognitionLanguages=@[@"en-US"]; request.usesLanguageCorrection=YES;
        VNImageRequestHandler* handler=[[VNImageRequestHandler alloc] initWithCGImage:cg options:@{}]; NSError* error=nil;
        BOOL performed=[handler performRequests:@[request] error:&error]; BOOL recognized=NO;
        for(VNRecognizedTextObservation* line in request.results) if([[[line topCandidates:1].firstObject string] containsString:@"Cliptiq"]) recognized=YES;
        [handler release]; [request release]; [image release];
        return !performed ? "engine_error" : (recognized ? "recognized" : "completed_text_mismatch");
    }
}
