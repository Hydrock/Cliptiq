// Compile with the pinned downloader source; only transport is synthetic.
#import <Foundation/Foundation.h>
#import "SPUDownloader.h"
#import "SPUDownloaderDelegate.h"
#import "SPULocalCacheDirectory.h"
#import "SPUDownloadData.h"
NSString *const SUSparkleErrorDomain=@"SparkleErrorDomain";
@implementation SPULocalCacheDirectory
+(NSString*)cachePathForBundleIdentifier:(NSString*)identifier{(void)identifier;abort();}
+(void)removeOldItemsInDirectory:(NSString*)directory{(void)directory;abort();}
+(NSString*)createUniqueDirectoryInDirectory:(NSString*)directory{(void)directory;abort();}
@end
@interface CliptiqFixtureProtocol:NSURLProtocol
@property(atomic)BOOL stopped;
@end
@implementation CliptiqFixtureProtocol
+(BOOL)canInitWithRequest:(NSURLRequest*)request{(void)request;return YES;}
+(NSURLRequest*)canonicalRequestForRequest:(NSURLRequest*)request{return request;}
-(void)stopLoading{self.stopped=YES;}
-(void)startLoading{
  dispatch_async(dispatch_get_global_queue(QOS_CLASS_DEFAULT,0),^{
    NSString*path=self.request.URL.path;
    NSUInteger count=[path isEqualToString:@"/small"]?1024:([path isEqualToString:@"/feed"]?128u*1024u+1:5u*1024u*1024u+1);
    // Unknown length defeats a Content-Length-only guard.
    NSHTTPURLResponse*response=[[NSHTTPURLResponse alloc]initWithURL:self.request.URL statusCode:200 HTTPVersion:@"HTTP/1.1" headerFields:@{}];
    [self.client URLProtocol:self didReceiveResponse:response cacheStoragePolicy:NSURLCacheStorageNotAllowed];
    while(count&&!self.stopped){NSUInteger n=MIN(count,16384u);[self.client URLProtocol:self didLoadData:[NSMutableData dataWithLength:n]];count-=n;[NSThread sleepForTimeInterval:0.001];}
    if(!self.stopped)[self.client URLProtocolDidFinishLoading:self];
  });
}
@end
static NSURLSessionConfiguration*CliptiqFixtureConfiguration(void){
  NSURLSessionConfiguration*configuration=[NSURLSessionConfiguration ephemeralSessionConfiguration];configuration.protocolClasses=@[CliptiqFixtureProtocol.class];return configuration;
}
#include "SPUDownloaderFixture.inc"
@interface CliptiqDelegate:NSObject<SPUDownloaderDelegate>
@property BOOL done,failed;
@property NSUInteger received;
@end
@implementation CliptiqDelegate
-(void)downloaderDidSetDownloadBookmarkData:(NSData*)data downloadToken:(NSString*)token{(void)data;(void)token;abort();}
-(void)downloaderDidReceiveExpectedContentLength:(int64_t)length{(void)length;}
-(void)downloaderDidReceiveDataOfLength:(uint64_t)length{self.received+=length;}
-(void)downloaderDidFinishWithTemporaryDownloadData:(SPUDownloadData*)data{self.done=YES;self.received=data.data.length;}
-(void)downloaderDidFailWithError:(NSError*)error{(void)error;self.done=YES;self.failed=YES;}
@end
int main(int argc,const char**argv){@autoreleasepool{
  BOOL red=argc>1&&strcmp(argv[1],"--red")==0;
  NSArray*cases=red?@[@"feed"]:@[@"small",@"feed",@"archive"];
  for(NSString*name in cases){
    CliptiqDelegate*delegate=[CliptiqDelegate new];SPUDownloader*downloader=[[SPUDownloader alloc]initWithDelegate:delegate];
    NSURLRequest*request=[NSURLRequest requestWithURL:[NSURL URLWithString:[@"https://github.com/" stringByAppendingString:name]]];
    if([name isEqualToString:@"archive"])[downloader startPersistentDownloadWithRequest:request bundleIdentifier:@"io.github.hydrock.cliptiq.fixture" desiredFilename:@"payload"];
    else[downloader startTemporaryDownloadWithRequest:request];
    NSDate*deadline=[NSDate dateWithTimeIntervalSinceNow:8];
    while(!delegate.done&&deadline.timeIntervalSinceNow>0)[NSRunLoop.mainRunLoop runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.005]];
    BOOL expectedFailure=![name isEqualToString:@"small"];
    BOOL passed=delegate.done&&delegate.failed==expectedFailure;
    if([name isEqualToString:@"small"])passed=passed&&delegate.received==1024;
    if([name isEqualToString:@"archive"])passed=passed&&delegate.received<=5u*1024u*1024u;
    [downloader cleanup:nil];
    if(!passed){fprintf(stderr,"bounds rejected test: %s\n",name.UTF8String);return 1;}
  }
  puts("native downloader: valid response, unknown-length appcast and archive byte ceilings passed");return 0;
}}
