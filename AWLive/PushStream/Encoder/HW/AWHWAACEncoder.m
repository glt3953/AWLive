/*
 copyright 2016 wanghongyu.
 The project page：https://github.com/hardman/AWLive
 My blog page: http://www.jianshu.com/u/1240d2400ca1
 */

#import "AWHWAACEncoder.h"
#import <VideoToolbox/VideoToolbox.h>
#import "AWEncoderManager.h"

@interface AWHWAACEncoder()
//audio params
@property (nonatomic, strong) NSData *curFramePcmData;

@property (nonatomic, unsafe_unretained) AudioConverterRef aConverter;
@property (nonatomic, unsafe_unretained) uint32_t aMaxOutputFrameSize;

@property (nonatomic, unsafe_unretained) aw_faac_config faacConfig;
@end

@implementation AWHWAACEncoder

//回调函数，系统指定格式
static OSStatus aacEncodeInputDataProc(AudioConverterRef inAudioConverter, UInt32 *ioNumberDataPackets, AudioBufferList *ioData, AudioStreamPacketDescription **outDataPacketDescription, void *inUserData){
    AWHWAACEncoder *hwAacEncoder = (__bridge AWHWAACEncoder *)inUserData;
    //将pcm数据交给编码器
    if (hwAacEncoder.curFramePcmData) {
        ioData->mBuffers[0].mData = (void *)hwAacEncoder.curFramePcmData.bytes;
        ioData->mBuffers[0].mDataByteSize = (uint32_t)hwAacEncoder.curFramePcmData.length;
        ioData->mNumberBuffers = 1;
        ioData->mBuffers[0].mNumberChannels = (uint32_t)hwAacEncoder.audioConfig.channelCount;
        
        return noErr;
    }
    
    return -1;
}

-(aw_flv_audio_tag *)encodePCMDataToFlvTag:(NSData *)pcmData{
    self.curFramePcmData = pcmData;
    
    //构造输出结构体，编码器需要
    AudioBufferList outAudioBufferList = {0};
    outAudioBufferList.mNumberBuffers = 1;
    outAudioBufferList.mBuffers[0].mNumberChannels = (uint32_t)self.audioConfig.channelCount;
    outAudioBufferList.mBuffers[0].mDataByteSize = self.aMaxOutputFrameSize;
    outAudioBufferList.mBuffers[0].mData = malloc(self.aMaxOutputFrameSize);
    
    uint32_t outputDataPacketSize = 1;
    
    //执行编码，此处需要传一个回调函数aacEncodeInputDataProc，以同步的方式，在回调中填充pcm数据。
    OSStatus status = AudioConverterFillComplexBuffer(_aConverter, aacEncodeInputDataProc, (__bridge void * _Nullable)(self), &outputDataPacketSize, &outAudioBufferList, NULL);
    if (status == noErr) {
        //编码成功，获取数据
        NSData *rawAAC = [NSData dataWithBytesNoCopy: outAudioBufferList.mBuffers[0].mData length:outAudioBufferList.mBuffers[0].mDataByteSize];
        //时间戳(ms) = 1000 * 每秒采样数 / 采样率;
        self.manager.timestamp += 1024 * 1000 / self.audioConfig.sampleRate;
        //获取到aac数据，转成flv audio tag，发送给服务端。
        return aw_encoder_create_audio_tag((int8_t *)rawAAC.bytes, rawAAC.length, (uint32_t)self.manager.timestamp, &_faacConfig);
    }else{
        [self onErrorWithCode:AWEncoderErrorCodeAudioEncoderFailed des:@"aac 编码错误"];
    }
    
    return NULL;
}

/**
 获取audio specific config，这是一个特别的flv tag，存储了使用的aac的一些关键数据，作为解析音频帧的基础。
 在rtmp中，必须将此帧在所有音频帧之前发送。
 */
-(aw_flv_audio_tag *)createAudioSpecificConfigFlvTag{
    //AudioSpecificConfig中包含3种元素：profile，sampleRate，channelCount，结构是：profile(5bit)-sampleRate(4bit)-channelCount(4bit)-空(3bit)
    //profile，表示使用的协议
    uint8_t profile = kMPEG4Object_AAC_LC;
    //采样率
    uint8_t sampleRate = 4;
    //channel信息
    uint8_t chanCfg = 1;
    //将上面3个信息拼在一起，成为2字节
    uint8_t config1 = (profile << 3) | ((sampleRate & 0xe) >> 1);
    uint8_t config2 = ((sampleRate & 0x1) << 7) | (chanCfg << 3);
    
    //将数据转成aw_data，写入config_data中
    aw_data *config_data = NULL;
    data_writer.write_uint8(&config_data, config1);
    data_writer.write_uint8(&config_data, config2);
    
    //转成flv tag
    aw_flv_audio_tag *audio_specific_config_tag = aw_encoder_create_audio_specific_config_tag(config_data, &_faacConfig);
    
    free_aw_data(&config_data);
    
    //返回给调用方，准备发送
    return audio_specific_config_tag;
}

-(void)open{
    //创建audio encode converter也就是AAC编码器
    //初始化一系列参数
    AudioStreamBasicDescription inputAudioDes = {
        .mFormatID = kAudioFormatLinearPCM,
        .mSampleRate = self.audioConfig.sampleRate,
        .mBitsPerChannel = (uint32_t)self.audioConfig.sampleSize,
        .mFramesPerPacket = 1,//每个包1帧
        .mBytesPerFrame = 2,//每帧2字节
        .mBytesPerPacket = 2,//每个包1帧也是2字节
        .mChannelsPerFrame = (uint32_t)self.audioConfig.channelCount,//声道数，推流一般使用单声道
        //下面这个flags的设置参照此文：http://www.mamicode.com/info-detail-986202.html
        .mFormatFlags = kLinearPCMFormatFlagIsPacked | kLinearPCMFormatFlagIsSignedInteger | kLinearPCMFormatFlagIsNonInterleaved,
        .mReserved = 0
    };
    
    //设置输出格式，声道数
    AudioStreamBasicDescription outputAudioDes = {
        .mChannelsPerFrame = (uint32_t)self.audioConfig.channelCount,
        .mFormatID = kAudioFormatMPEG4AAC,
        0
    };
    
    //初始化_aConverter
    uint32_t outDesSize = sizeof(outputAudioDes);
    AudioFormatGetProperty(kAudioFormatProperty_FormatInfo, 0, NULL, &outDesSize, &outputAudioDes);
    OSStatus status = AudioConverterNew(&inputAudioDes, &outputAudioDes, &_aConverter);
    if (status != noErr) {
        [self onErrorWithCode:AWEncoderErrorCodeCreateAudioConverterFailed des:@"硬编码AAC创建失败"];
    }
    
    //设置码率
    uint32_t aBitrate = (uint32_t)self.audioConfig.bitrate;
    uint32_t aBitrateSize = sizeof(aBitrate);
    status = AudioConverterSetProperty(_aConverter, kAudioConverterEncodeBitRate, aBitrateSize, &aBitrate);
    
    //查询最大输出
    uint32_t aMaxOutput = 0;
    uint32_t aMaxOutputSize = sizeof(aMaxOutput);
    AudioConverterGetProperty(_aConverter, kAudioConverterPropertyMaximumOutputPacketSize, &aMaxOutputSize, &aMaxOutput);
    self.aMaxOutputFrameSize = aMaxOutput;
    if (aMaxOutput == 0) {
        [self onErrorWithCode:AWEncoderErrorCodeAudioConverterGetMaxFrameSizeFailed des:@"AAC 获取最大frame size失败"];
    }
}

-(void)close{
    AudioConverterDispose(_aConverter);
    _aConverter = nil;
    self.curFramePcmData = nil;
    self.aMaxOutputFrameSize = 0;
}

@end
