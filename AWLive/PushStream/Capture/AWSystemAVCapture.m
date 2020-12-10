/*
 copyright 2016 wanghongyu.
 The project page：https://github.com/hardman/AWLive
 My blog page: http://www.jianshu.com/u/1240d2400ca1
 */

#import "AWSystemAVCapture.h"
#import <AVFoundation/AVFoundation.h>
#import <UIKit/UIKit.h>

@interface AWSystemAVCapture ()<AVCaptureVideoDataOutputSampleBufferDelegate, AVCaptureAudioDataOutputSampleBufferDelegate>
//前后摄像头
@property (nonatomic, strong) AVCaptureDeviceInput *frontCamera;
@property (nonatomic, strong) AVCaptureDeviceInput *backCamera;

//当前使用的视频设备
@property (nonatomic, weak) AVCaptureDeviceInput *videoInputDevice;
//音频设备
@property (nonatomic, strong) AVCaptureDeviceInput *audioInputDevice;

//输出数据接收
@property (nonatomic, strong) AVCaptureVideoDataOutput *videoDataOutput;
@property (nonatomic, strong) AVCaptureAudioDataOutput *audioDataOutput;

//会话
@property (nonatomic, strong) AVCaptureSession *captureSession;

//预览
@property (nonatomic, strong) AVCaptureVideoPreviewLayer *previewLayer;

@end

@implementation AWSystemAVCapture

-(void)switchCamera{
    if ([self.videoInputDevice isEqual: self.frontCamera]) {
        self.videoInputDevice = self.backCamera;
    }else{
        self.videoInputDevice = self.frontCamera;
    }
    
    //更新fps
    [self updateFps: self.videoConfig.fps];
}

-(void)onInit{
    [self createCaptureDevice];
    [self createOutput];
    [self createCaptureSession];
    [self createPreviewLayer];
    
    //更新fps
    [self updateFps: self.videoConfig.fps];
}

//初始化视频设备
-(void) createCaptureDevice{
    //创建视频设备
    NSArray *videoDevices = [AVCaptureDevice devicesWithMediaType:AVMediaTypeVideo];
    // 初始化前后摄像头
    // 执行这几句代码后，系统会弹框提示：应用想要访问您的相机。请点击同意
    // 另外iOS10 需要在info.plist中添加字段NSCameraUsageDescription。否则会闪退，具体请自行baidu。
    self.frontCamera = [AVCaptureDeviceInput deviceInputWithDevice:videoDevices.firstObject error:nil];
    self.backCamera =[AVCaptureDeviceInput deviceInputWithDevice:videoDevices.lastObject error:nil];
    
    // 初始化麦克风
    // 执行这几句代码后，系统会弹框提示：应用想要访问您的麦克风。请点击同意
    // 另外iOS10 需要在info.plist中添加字段NSMicrophoneUsageDescription。否则会闪退，具体请自行baidu。
    AVCaptureDevice *audioDevice = [AVCaptureDevice defaultDeviceWithMediaType:AVMediaTypeAudio];
    self.audioInputDevice = [AVCaptureDeviceInput deviceInputWithDevice:audioDevice error:nil];
    
    self.videoInputDevice = self.frontCamera;
}

//切换摄像头
-(void)setVideoInputDevice:(AVCaptureDeviceInput *)videoInputDevice{
    if ([videoInputDevice isEqual:_videoInputDevice]) {
        return;
    }
    
    //captureSession 修改配置
    [self.captureSession beginConfiguration];
    //移除当前输入设备
    if (_videoInputDevice) {
        [self.captureSession removeInput:_videoInputDevice];
    }
    //增加新的输入设备
    if (videoInputDevice) {
        [self.captureSession addInput:videoInputDevice];
    }
    
    [self setVideoOutConfig];
    
    //提交配置，至此前后摄像头切换完毕
    [self.captureSession commitConfiguration];
    
    _videoInputDevice = videoInputDevice;
}

//创建预览
// 其实只有一句代码：CALayer layer = [AVCaptureVideoPreviewLayer layerWithSession:self.captureSession];
// 它其实是 AVCaptureSession的一个输出方式而已。
// CaptureSession会将从input设备得到的数据，处理后，显示到此layer上。
// 我们可以将此layer变换后加入到任意UIView中。
-(void) createPreviewLayer{
    self.previewLayer = [AVCaptureVideoPreviewLayer layerWithSession:self.captureSession];
    self.previewLayer.frame = self.preview.bounds;
    [self.preview.layer addSublayer:self.previewLayer];
}

-(void) setVideoOutConfig{
    for (AVCaptureConnection *conn in self.videoDataOutput.connections) {
        if (conn.isVideoStabilizationSupported) {
            [conn setPreferredVideoStabilizationMode:AVCaptureVideoStabilizationModeAuto];
        }
        if (conn.isVideoOrientationSupported) {
            [conn setVideoOrientation:AVCaptureVideoOrientationPortrait];
        }
        if (conn.isVideoMirrored) {
            [conn setVideoMirrored: YES];
        }
    }
}

//创建会话，AVCaptureSession 创建逻辑很简单，它像是一个中介者，从音视频输入设备获取数据，处理后，传递给输出设备(数据代理/预览layer)。
-(void) createCaptureSession{
    //初始化
    self.captureSession = [AVCaptureSession new];
    
    //修改配置
    [self.captureSession beginConfiguration];
    
    //加入视频输入设备
    if ([self.captureSession canAddInput:self.videoInputDevice]) {
        [self.captureSession addInput:self.videoInputDevice];
    }
    
    //加入音频输入设备
    if ([self.captureSession canAddInput:self.audioInputDevice]) {
        [self.captureSession addInput:self.audioInputDevice];
    }
    
    //加入视频输出
    if([self.captureSession canAddOutput:self.videoDataOutput]){
        [self.captureSession addOutput:self.videoDataOutput];
        [self setVideoOutConfig];
    }
    
    //加入音频输出
    if([self.captureSession canAddOutput:self.audioDataOutput]){
        [self.captureSession addOutput:self.audioDataOutput];
    }
    
    //设置预览分辨率
    //这个分辨率有一个值得注意的点：
    //iphone4录制视频时 前置摄像头只能支持 480*640 后置摄像头不支持 540*960 但是支持 720*1280
    //诸如此类的限制，所以需要写一些对分辨率进行管理的代码。
    //目前的处理是，对于不支持的分辨率会抛出一个异常
    //但是这样做是不够、不完整的，最好的方案是，根据设备，提供不同的分辨率。
    //如果必须要用一个不支持的分辨率，那么需要根据需求对数据和预览进行裁剪，缩放。
    if (![self.captureSession canSetSessionPreset:self.captureSessionPreset]) {
        @throw [NSException exceptionWithName:@"Not supported captureSessionPreset" reason:[NSString stringWithFormat:@"captureSessionPreset is [%@]", self.captureSessionPreset] userInfo:nil];
    }
    
    self.captureSession.sessionPreset = self.captureSessionPreset;
    
    //提交配置变更
    [self.captureSession commitConfiguration];
    
    //开始运行，此时，CaptureSession将从输入设备获取数据，处理后，传递给输出设备。
    [self.captureSession startRunning];
}

//销毁会话
-(void) destroyCaptureSession{
    if (self.captureSession) {
        [self.captureSession removeInput:self.audioInputDevice];
        [self.captureSession removeInput:self.videoInputDevice];
        [self.captureSession removeOutput:self.self.videoDataOutput];
        [self.captureSession removeOutput:self.self.audioDataOutput];
    }
    self.captureSession = nil;
}

-(void) createOutput{
    //创建数据获取线程
    dispatch_queue_t captureQueue = dispatch_queue_create("aw.capture.queue", DISPATCH_QUEUE_SERIAL);
    
    //视频数据输出
    self.videoDataOutput = [[AVCaptureVideoDataOutput alloc] init];
    //设置代理，需要当前类实现protocol：AVCaptureVideoDataOutputSampleBufferDelegate
    [self.videoDataOutput setSampleBufferDelegate:self queue:captureQueue];
    //抛弃过期帧，保证实时性
    [self.videoDataOutput setAlwaysDiscardsLateVideoFrames:YES];
    //设置输出格式为 yuv420
    [self.videoDataOutput setVideoSettings:@{
                                             (__bridge NSString *)kCVPixelBufferPixelFormatTypeKey:@(kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange)
                                             }];
    
    //音频数据输出
    self.audioDataOutput = [[AVCaptureAudioDataOutput alloc] init];
    //设置代理，需要当前类实现protocol：AVCaptureAudioDataOutputSampleBufferDelegate
    [self.audioDataOutput setSampleBufferDelegate:self queue:captureQueue];
    
    // AVCaptureVideoDataOutputSampleBufferDelegate 和 AVCaptureAudioDataOutputSampleBufferDelegate 回调方法名相同都是：
    // captureOutput:(AVCaptureOutput *)captureOutput didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer fromConnection:(AVCaptureConnection *)connection
    // 最终视频和音频数据都可以在此方法中获取。
}

-(void)captureOutput:(AVCaptureOutput *)captureOutput didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer fromConnection:(AVCaptureConnection *)connection{
    if (self.isCapturing) {
        if ([self.videoDataOutput isEqual:captureOutput]) {
            //捕获到视频数据，通过sendVideoSampleBuffer发送出去，后续文章会解释接下来的详细流程。
            [self sendVideoSampleBuffer:sampleBuffer];
        }else if([self.audioDataOutput isEqual:captureOutput]){
            //捕获到音频数据，通过sendVideoSampleBuffer发送出去
            [self sendAudioSampleBuffer:sampleBuffer];
        }
    }
}

@end
