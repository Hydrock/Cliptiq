"""Apply Cliptiq byte bounds to exactly the pinned Sparkle2.10.0 downloader.

The original license remains in the source/framework. Both the in-process and
XPC downloader targets compile this same file. No installer/verifier is changed.
"""
from pathlib import Path
import hashlib,sys
def patch(root):
 p=Path(root)/'Downloader/SPUDownloader.m';s=p.read_text(encoding="utf-8")
 assert hashlib.sha256(p.read_bytes()).hexdigest()=='3f5c760d96634562716654090c75ef28ec4e1f53809dfce613c92a86534d3ad0'
 s=s.replace('<NSURLSessionDownloadDelegate>','<NSURLSessionDataDelegate>')
 s=s.replace('    BOOL _receivedExpectedBytes;','''    BOOL _receivedExpectedBytes;
    // Cliptiq: bounded retained response; no system downloadTask/cache files.
    NSMutableData *_cliptiqData;
    NSError *_cliptiqFailure;
    NSURL *_cliptiqDirectory;''')
 a=s.index('    _downloadSession = [NSURLSession');b=s.index('    [_sessionTask resume];',a)
 s=s[:a]+'''    NSURLSessionConfiguration *configuration = [NSURLSessionConfiguration ephemeralSessionConfiguration];
    configuration.URLCache = nil;
    configuration.requestCachePolicy = NSURLRequestReloadIgnoringLocalCacheData;
    configuration.timeoutIntervalForRequest = 15;
    configuration.timeoutIntervalForResource = 20;
    _cliptiqData = [NSMutableData data];
    _downloadSession = [NSURLSession sessionWithConfiguration:configuration delegate:self delegateQueue:[NSOperationQueue mainQueue]];
    _sessionTask = [_downloadSession dataTaskWithRequest:request];

'''+s[b:]
 s=s.replace('    _delegate = nil;','''    _delegate = nil;
    _cliptiqData = nil;
    if (_cliptiqDirectory != nil) {
        [[NSFileManager defaultManager] removeItemAtURL:_cliptiqDirectory error:NULL];
        _cliptiqDirectory = nil;
    }''')
 s=s.replace('- (void)URLSession:(NSURLSession *)__unused session downloadTask:(NSURLSessionDownloadTask *)downloadTask didFinishDownloadingToURL:(NSURL *)location','- (void)cliptiqFinishPersistentTask:(NSURLSessionTask *)downloadTask location:(NSURL *)location')
 a=s.index('- (void)URLSession:(NSURLSession *)__unused session downloadTask:');s=s[:a]+'''
- (NSUInteger)cliptiqLimit
{
    return _mode == SPUDownloadModeTemporary ? 128u * 1024u : 5u * 1024u * 1024u;
}

- (void)URLSession:(NSURLSession *)session dataTask:(NSURLSessionDataTask *)task didReceiveResponse:(NSURLResponse *)response completionHandler:(void (^)(NSURLSessionResponseDisposition))completion
{
    (void)session;
    if (response.expectedContentLength > (int64_t)[self cliptiqLimit]) {
        _cliptiqFailure = [NSError errorWithDomain:SUSparkleErrorDomain code:SUDownloadError userInfo:@{NSLocalizedDescriptionKey: @"Cliptiq response exceeds byte limit"}];
        completion(NSURLSessionResponseCancel);
        return;
    }
    if (_mode == SPUDownloadModePersistent) {
        _receivedExpectedBytes = YES;
        [_delegate downloaderDidReceiveExpectedContentLength:MAX((int64_t)0, response.expectedContentLength)];
    }
    completion(NSURLSessionResponseAllow);
}

- (void)URLSession:(NSURLSession *)session dataTask:(NSURLSessionDataTask *)task didReceiveData:(NSData *)data
{
    (void)session;
    // Check decoded bytes before appending, including unknown/chunked length.
    NSUInteger limit = [self cliptiqLimit];
    if (_cliptiqFailure != nil) return;
    if (data.length > limit || _cliptiqData.length > limit - data.length) {
        _cliptiqFailure = [NSError errorWithDomain:SUSparkleErrorDomain code:SUDownloadError userInfo:@{NSLocalizedDescriptionKey: @"Cliptiq actual response exceeds byte limit"}];
        [_cliptiqData setLength:0];
        [task cancel];
        return;
    }
    [_cliptiqData appendData:data];
    if (_mode == SPUDownloadModePersistent) [_delegate downloaderDidReceiveDataOfLength:data.length];
}

- (void)URLSession:(NSURLSession *)session task:(NSURLSessionTask *)task didCompleteWithError:(NSError *)error
{
    (void)session;
    NSError *failure = _cliptiqFailure != nil ? _cliptiqFailure : error;
    if (failure != nil) {
        [_delegate downloaderDidFailWithError:failure];
        [self _cleanup];
        return;
    }
    if (_mode == SPUDownloadModeTemporary) {
        [self temporaryDownloadDidFinishWithData:_cliptiqData response:task.response error:nil];
        return;
    }
    // Only a complete bounded response reaches disk. Private directory creation
    // is exclusive and never reuses/removes an arbitrary existing directory.
    NSURL *directory = [NSURL fileURLWithPath:[NSTemporaryDirectory() stringByAppendingPathComponent:[@"cliptiq-update-" stringByAppendingString:NSUUID.UUID.UUIDString]] isDirectory:YES];
    NSError *writeError = nil;
    BOOL made = [[NSFileManager defaultManager] createDirectoryAtURL:directory withIntermediateDirectories:NO attributes:@{NSFilePosixPermissions: @0700} error:&writeError];
    if (made) _cliptiqDirectory = directory;
    NSURL *file = [directory URLByAppendingPathComponent:@"payload"];
    if (!made || ![_cliptiqData writeToURL:file options:NSDataWritingWithoutOverwriting error:&writeError]) {
        [_delegate downloaderDidFailWithError:writeError];
    } else {
        [self cliptiqFinishPersistentTask:task location:file];
    }
    [self _cleanup];
}

@end
'''
 p.write_bytes(s.encode("utf-8"))
 return hashlib.sha256(p.read_bytes()).hexdigest()
if __name__=='__main__':print(patch(sys.argv[1]))
