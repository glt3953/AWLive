/*
 copyright 2016 wanghongyu.
 The project page：https://github.com/hardman/AWLive
 My blog page: http://www.jianshu.com/u/1240d2400ca1
 */

#include "aw_encode_flv.h"
#include "aw_alloc.h"
#include <string.h>
#include "aw_utils.h"

extern aw_flv_script_tag *alloc_aw_flv_script_tag(){
    aw_flv_script_tag *script_tag = aw_alloc(sizeof(aw_flv_script_tag));
    memset(script_tag, 0, sizeof(aw_flv_script_tag));
    
    //初始化
    script_tag->common_tag.script_tag = script_tag;
    script_tag->common_tag.tag_type = aw_flv_tag_type_script;
    
    script_tag->v_codec_id = aw_flv_v_codec_id_H264;
    script_tag->a_codec_id = aw_flv_a_codec_id_AAC;
    
    //经计算，我们写入的script 的body size为255
    script_tag->common_tag.data_size = 255;
    
    return script_tag;
}

extern void free_aw_flv_script_tag(aw_flv_script_tag ** script_tag){
    aw_free(*script_tag);
    *script_tag = NULL;
}

extern aw_flv_audio_tag *alloc_aw_flv_audio_tag(){
    aw_flv_audio_tag *audio_tag = aw_alloc(sizeof(aw_flv_audio_tag));
    memset(audio_tag, 0, sizeof(aw_flv_audio_tag));
    //初始化
    audio_tag->common_tag.audio_tag = audio_tag;
    audio_tag->common_tag.tag_type = aw_flv_tag_type_audio;
    
    return audio_tag;
}

extern void free_aw_flv_audio_tag(aw_flv_audio_tag ** audio_tag){
    if ((*audio_tag)->config_record_data) {
        free_aw_data(&(*audio_tag)->config_record_data);
    }
    if((*audio_tag)->frame_data){
        free_aw_data(&(*audio_tag)->frame_data);
    }
    aw_free(*audio_tag);
    *audio_tag = NULL;
}

extern aw_flv_video_tag *alloc_aw_flv_video_tag(){
    aw_flv_video_tag *video_tag = aw_alloc(sizeof(aw_flv_video_tag));
    memset(video_tag, 0, sizeof(aw_flv_video_tag));
    //初始化
    video_tag->common_tag.video_tag = video_tag;
    video_tag->common_tag.tag_type = aw_flv_tag_type_video;
    
    return video_tag;
}

extern void free_aw_flv_video_tag(aw_flv_video_tag **video_tag){
    if ((*video_tag)->config_record_data) {
        free_aw_data(&(*video_tag)->config_record_data);
    }
    if((*video_tag)->frame_data){
        free_aw_data(&(*video_tag)->frame_data);
    }
    aw_free(*video_tag);
    *video_tag = NULL;
}

static void aw_write_tag_header(aw_data **flv_data, aw_flv_common_tag *common_tag){
    //header 长度为固定11个字节
    //写入tag type，video：9 audio：8 script：18
    data_writer.write_uint8(flv_data, common_tag->tag_type);
    //写入body的size(data_size为整个tag的长度)
    data_writer.write_uint24(flv_data, common_tag->data_size - 11);
    //写入时间戳
    data_writer.write_uint24(flv_data, common_tag->timestamp);
    data_writer.write_uint8(flv_data, common_tag->timestamp_extend);
    //写入stream id为0
    data_writer.write_uint24(flv_data, common_tag->stream_id);
}

static void aw_write_audio_tag_body(aw_data **flv_data, aw_flv_audio_tag *audio_tag){
    // audio tag body的结构是这样的：
    // sound_format(4bit) + sound_rate(sample_rate)(2bit) + sound_size(sample_size)(1bit) + sound_type(1bit) + aac_packet_type(8bit) + aac_data(many bits)
    // sound_format 表示声音格式，2表示mp3，10表示aac，一般是aac
    // sound_rate 采样率，表示1秒钟采集多少个样本，可选4个值，0表示5.5kHZ，1表示11kHZ，2表示22kHZ，3表示44kHZ，一般是3。
    // sound_size 采样尺寸，单个样本的size。2个选择，0表示8bit，1表示16bit。
    // 直观上看，采样率和采样尺寸应该和质量有一定关系。采样率高，采样尺寸大效果应该会好，但是生成的数据量也大。
    // sound_type 表示声音类型，0表示单声道，1表示立体声。(立体声有2条声道)。
    // aac_packet_type表示aac数据类型，有2种选择：0表示sequence header，即 必须首帧发送的数据(AudioSpecificConfig)，1表示正常的aac数据。
    
    uint8_t audio_header = 0;
    audio_header |= audio_tag->sound_format << 4 & 0xf0;
    audio_header |= audio_tag->sound_rate << 2 & 0xc;
    audio_header |= audio_tag->sound_size << 1 & 0x2;
    audio_header |= audio_tag->sound_type & 0x1;
    data_writer.write_uint8(flv_data, audio_header);
    
    if (audio_tag->sound_format == aw_flv_a_codec_id_AAC) {
        data_writer.write_uint8(flv_data, audio_tag->aac_packet_type);
    }
    switch (audio_tag->aac_packet_type) {
        case aw_flv_a_aac_package_type_aac_sequence_header: {
            data_writer.write_bytes(flv_data, audio_tag->config_record_data->data, audio_tag->config_record_data->size);
            break;
        }
        case aw_flv_a_aac_package_type_aac_raw: {
            data_writer.write_bytes(flv_data, audio_tag->frame_data->data, audio_tag->frame_data->size);
            break;
        }
    }
}

static void aw_write_video_tag_body(aw_data **flv_data, aw_flv_video_tag *video_tag){
    // video tag body 结构是这样的：
    // frame_type(4bit) + codec_id(4bit) + h264_package_type(8bit) + h264_composition_time(24bit) + video_tag_data(many bits)
    // frame_type 表示是否关键帧，关键帧为1，非关键帧为2（当然还有更多取值，请参考[flv协议](https://wuyuans.com/img/2012/08/video_file_format_spec_v10.rar)
    // codec_id 表示视频协议：h264是7 h263是2。
    // h264_package_type表示视频帧数据的类型，2种取值：sequence header（也就是前面说的 sps pps 数据，rtmp要求首帧发送此数据，也称为AVCDecoderConfigurationRecord），另一种为nalu，正常的h264视频帧。
    // h264_compsition_time：cts是pts与dts的差值，flv中的timestamp表示的应该是pts。如果h264数据中不包含B帧，那么此数据可传0。
    // video_tag_data 即纯264数据。
    
    uint8_t video_header = 0;
    video_header |= video_tag->frame_type << 4 & 0xf0;
    video_header |= video_tag->codec_id & 0x0f;
    data_writer.write_uint8(flv_data, video_header);
    
    if (video_tag->codec_id == aw_flv_v_codec_id_H264) {
        data_writer.write_uint8(flv_data, video_tag->h264_package_type);
        data_writer.write_uint24(flv_data, video_tag->h264_composition_time);
    }
    
    switch (video_tag->h264_package_type) {
        case aw_flv_v_h264_packet_type_seq_header: {
            data_writer.write_bytes(flv_data, video_tag->config_record_data->data, video_tag->config_record_data->size);
            break;
        }
        case aw_flv_v_h264_packet_type_nalu: {
            data_writer.write_bytes(flv_data, video_tag->frame_data->data, video_tag->frame_data->size);
            break;
        }
        case aw_flv_v_h264_packet_type_end_of_seq: {
            //nothing
            break;
        }
    }
}

static void aw_write_script_tag_body(aw_data **flv_data, aw_flv_script_tag *script_tag){
    //script tag写入规则为：类型-内容-类型-内容...类型-内容
    //类型是1个字节整数，可取12种值：
    //    0 = Number type
    //    1 = Boolean type
    //    2 = String type
    //    3 = Object type
    //    4 = MovieClip type
    //    5 = Null type
    //    6 = Undefined type
    //    7 = Reference type
    //    8 = ECMA array type
    //    10 = Strict array type
    //    11 = Date type
    //    12 = Long string type
    // 比如：如果类型是字符串，那么先写入1个字节表类型的2。另，写入真正的字符串前，需要写入2个字节的字符串长度。
    // data_writer.write_string能够在写入字符串前，先写入字符串长度，此函数第三个参数表示用多少字节来存储字符串长度。
    // script tag 的结构基本上是固定的，首先写入一个字符串: onMetaData，然后写入一个数组。
    // 写入数组需要先写入数组编号1字节：8，然后写入数组长度4字节：11。
    // 数组同OC的Dictionary类似，可写入一个字符串+一个value。
    // 所以每个数组元素可先写入一个字符串，然后写入一个Number Type，再写入具体的数值。
    // 结束时需写入3个字节的0x000009表示数组结束。
    // 下面代码中的duration/width/filesize均遵循此规则。
    
    //纪录写入了多少字节
    data_writer.end_record_size();
    data_writer.start_record_size();
    
    //2表示类型，字符串
    data_writer.write_uint8(flv_data, 2);
    data_writer.write_string(flv_data, "onMetaData", 2);
    
    //数组类型：8
    data_writer.write_uint8(flv_data, 8);
    //数组长度：11
    data_writer.write_uint32(flv_data, 11);
    
    //28字节
    
    //写入duration 0表示double，1表示uint8
    data_writer.write_string(flv_data, "duration", 2);
    data_writer.write_uint8(flv_data, 0);
    data_writer.write_double(flv_data, script_tag->duration);
    //写入width
    data_writer.write_string(flv_data, "width", 2);
    data_writer.write_uint8(flv_data, 0);
    data_writer.write_double(flv_data, script_tag->width);
    //写入height
    data_writer.write_string(flv_data, "height", 2);
    data_writer.write_uint8(flv_data, 0);
    data_writer.write_double(flv_data, script_tag->height);
    //写入videodatarate
    data_writer.write_string(flv_data, "videodatarate", 2);
    data_writer.write_uint8(flv_data, 0);
    data_writer.write_double(flv_data, script_tag->video_data_rate);
    //写入framerate
    data_writer.write_string(flv_data, "framerate", 2);
    data_writer.write_uint8(flv_data, 0);
    data_writer.write_double(flv_data, script_tag->frame_rate);
    //写入videocodecid
    data_writer.write_string(flv_data, "videocodecid", 2);
    data_writer.write_uint8(flv_data, 0);
    data_writer.write_double(flv_data, script_tag->v_codec_id);
    //写入audiosamplerate
    data_writer.write_string(flv_data, "audiosamplerate", 2);
    data_writer.write_uint8(flv_data, 0);
    data_writer.write_double(flv_data, script_tag->a_sample_rate);
    //写入audiosamplesize
    data_writer.write_string(flv_data, "audiosamplesize", 2);
    data_writer.write_uint8(flv_data, 0);
    data_writer.write_double(flv_data, script_tag->a_sample_size);
    //写入stereo
    data_writer.write_string(flv_data, "stereo", 2);
    data_writer.write_uint8(flv_data, 1);
    data_writer.write_uint8(flv_data, script_tag->stereo);
    //写入a_codec_id
    data_writer.write_string(flv_data, "audiocodecid", 2);
    data_writer.write_uint8(flv_data, 0);
    data_writer.write_double(flv_data, script_tag->a_codec_id);
    //写入file_size
    data_writer.write_string(flv_data, "filesize", 2);
    data_writer.write_uint8(flv_data, 0);
    data_writer.write_double(flv_data, script_tag->file_size);
    
    //3字节的0x9表示metadata结束
    data_writer.write_uint24(flv_data, 9);
    
    //打印写入了多少字节
    AWLog("script tag body size = %ld", data_writer.record_size());
    data_writer.end_record_size();
}

static void aw_write_tag_body(aw_data **flv_data, aw_flv_common_tag *common_tag){
    switch (common_tag->tag_type) {
        case aw_flv_tag_type_audio: {
            aw_write_audio_tag_body(flv_data, common_tag->audio_tag);
            break;
        }
        case aw_flv_tag_type_video: {
            aw_write_video_tag_body(flv_data, common_tag->video_tag);
            break;
        }
        case aw_flv_tag_type_script: {
            aw_write_script_tag_body(flv_data, common_tag->script_tag);
            break;
        }
    }
}

static void aw_write_tag_data_size(aw_data **flv_data, aw_flv_common_tag *common_tag){
    data_writer.write_uint32(flv_data, common_tag->data_size);
}

/**
 flv的body是由一个接一个的tag构成的。
 一个flv tag分为3部分：tag header + tag body + tag data size。
 */
extern void aw_write_flv_tag(aw_data **flv_data, aw_flv_common_tag *common_tag){
    //写入header
    aw_write_tag_header(flv_data, common_tag);
    //写入body
    aw_write_tag_body(flv_data, common_tag);
    //写入data size
    aw_write_tag_data_size(flv_data, common_tag);
}

extern void aw_write_flv_header(aw_data **flv_data){
    uint8_t
    f = 'F', l = 'L', v = 'V',//FLV
    version = 1,//固定值
    av_flag = 5;//5表示av，5表示只有a，1表示只有v
    uint32_t flv_header_len = 9; //header固定长度为9
    data_writer.write_uint8(flv_data, f);
    data_writer.write_uint8(flv_data, l);
    data_writer.write_uint8(flv_data, v);
    data_writer.write_uint8(flv_data, version);
    data_writer.write_uint8(flv_data, av_flag);
    data_writer.write_uint32(flv_data, flv_header_len);
    
    //first previous tag size 根据flv协议，每个tag后要写入当前tag的size，称为previous tag size，header后面需要写入4字节空数据。
    data_writer.write_uint32(flv_data, 0);
}
