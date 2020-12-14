/*
 copyright 2016 wanghongyu.
 The project page：https://github.com/hardman/AWLive
 My blog page: http://www.jianshu.com/u/1240d2400ca1
 */

#include "aw_sw_faac_encoder.h"
#include "aw_all.h"

//faac 实例
static aw_faac_context *s_faac_ctx = NULL;
static aw_faac_config *s_faac_config = NULL;

//debug使用
static int32_t audio_count = 0;

//创建基本的audio tag，除类型，数据和时间戳
static aw_flv_audio_tag *aw_sw_encoder_create_flv_audio_tag(aw_faac_config *faac_cfg){
    aw_flv_audio_tag *audio_tag = alloc_aw_flv_audio_tag();
    audio_tag->sound_format = aw_flv_a_codec_id_AAC;
    audio_tag->common_tag.header_size = 2;
    
    if (faac_cfg->sample_rate == 22050) {
        audio_tag->sound_rate = aw_flv_a_sound_rate_22kHZ;
    }else if (faac_cfg->sample_rate == 11025) {
        audio_tag->sound_rate = aw_flv_a_sound_rate_11kHZ;
    }else if (faac_cfg->sample_rate == 5500) {
        audio_tag->sound_rate = aw_flv_a_sound_rate_5_5kHZ;
    }else{
        audio_tag->sound_rate = aw_flv_a_sound_rate_44kHZ;
    }
    
    if (faac_cfg->sample_size == 8) {
        audio_tag->sound_size = aw_flv_a_sound_size_8_bit;
    }else{
        audio_tag->sound_size = aw_flv_a_sound_size_16_bit;
    }
    
    audio_tag->sound_type = faac_cfg->channel_count == 1 ? aw_flv_a_sound_type_mono : aw_flv_a_sound_type_stereo;
    return audio_tag;
}

//编码器开关
/*
    faac_config：需要由上层传入相关配置属性
*/
extern void aw_sw_encoder_open_faac_encoder(aw_faac_config *faac_config){
    //是否已经开启了，避免重复开启
    if (aw_sw_faac_encoder_is_valid()) {
        aw_log("[E] aw_sw_encoder_open_faac_encoder when encoder is already inited");
        return;
    }
    
    //创建配置
    int32_t faac_cfg_len = sizeof(aw_faac_config);
    if (!s_faac_config) {
        s_faac_config = aw_alloc(faac_cfg_len);
    }
    memcpy(s_faac_config, faac_config, faac_cfg_len);
    
    //开启faac软编码
    s_faac_ctx = alloc_aw_faac_context(*faac_config);
}

//关闭编码器并释放资源
extern void aw_sw_encoder_close_faac_encoder(){
    if (!aw_sw_faac_encoder_is_valid()) {
        aw_log("[E] aw_sw_encoder_close_faac_encoder when encoder is not inited");
        return;
    }
    
    //是否aw_faac_context，也就关闭了faac编码环境。
    free_aw_faac_context(&s_faac_ctx);
    
    if (s_faac_config) {
        aw_free(s_faac_config);
        s_faac_config = NULL;
    }
}

//对pcm数据进行faac软编码，并转成flv_audio_tag
/*
    pcm_data: 传入的pcm数据
    len: pcm数据长度
    timestamp：flv时间戳，rtmp协议要求发送的flv音视频帧的时间戳需为均匀增加，不允许 后发送的数据时间戳 比 先发送的数据的时间戳 还要小。
    aw_flv_audio_tag: 返回类型，生成的flv音频数据（flv中，每帧数据称为一个tag）。
*/
extern aw_flv_audio_tag *aw_sw_encoder_encode_faac_data(int8_t *pcm_data, long len, uint32_t timestamp){
    if (!aw_sw_faac_encoder_is_valid()) {
        aw_log("[E] aw_sw_encoder_encode_faac_data when encoder is not inited");
        return NULL;
    }
    
    //将pcm数据编码成aac数据
    aw_encode_pcm_frame_2_aac(s_faac_ctx, pcm_data, len);
    
    // 使用faac编码的数据会带有7个字节的adts头。rtmp不接受此值，在此去掉前7个字节。
    int adts_header_size = 7;
    
    //除去ADTS头的7字节
    if (s_faac_ctx->encoded_aac_data->size <= adts_header_size) {
        return NULL;
    }
    
    //将aac数据封装成flv音频帧。flv帧仅仅是将aac数据增加一些固定信息。并没有对aac数据进行编码操作。
    aw_flv_audio_tag *audio_tag = aw_encoder_create_audio_tag((int8_t *)s_faac_ctx->encoded_aac_data->data + adts_header_size, s_faac_ctx->encoded_aac_data->size - adts_header_size, timestamp, &s_faac_ctx->config);
    
    audio_count++;
    
    return audio_tag;
}

//根据faac_config 创建包含audio specific config 的flv tag，将audio specific config data 转成flv帧数据。
extern aw_flv_audio_tag *aw_sw_encoder_create_faac_specific_config_tag(){
    //是否已打开编码器
    if(!aw_sw_faac_encoder_is_valid()){
        aw_log("[E] aw_sw_encoder_create_faac_specific_config_tag when audio encoder is not inited");
        return NULL;
    }
    
    //创建 audio specfic config record
    aw_flv_audio_tag *aac_tag = aw_sw_encoder_create_flv_audio_tag(&s_faac_ctx->config);
    //根据flv协议：audio specific data对应的 aac_packet_type 固定为 aw_flv_a_aac_package_type_aac_sequence_header 值为0
    //普通的音频帧，此处值为1.
    aac_tag->aac_packet_type = aw_flv_a_aac_package_type_aac_sequence_header;
    
    aac_tag->config_record_data = copy_aw_data(s_faac_ctx->audio_specific_config_data);
    aac_tag->common_tag.timestamp = 0;
    aac_tag->common_tag.data_size = s_faac_ctx->audio_specific_config_data->size + 11 + aac_tag->common_tag.header_size;
    
    return aac_tag;
}

extern uint32_t aw_sw_faac_encoder_max_input_sample_count(){
    if (aw_sw_faac_encoder_is_valid()) {
        return (uint32_t)s_faac_ctx->max_input_sample_count;
    }
    return 0;
}

//编码器是否合法
extern int8_t aw_sw_faac_encoder_is_valid(){
    return s_faac_ctx != NULL;
}

//下面2个函数所有编码器都可以用
//将aac数据转为flv_audio_tag
extern aw_flv_audio_tag *aw_encoder_create_audio_tag(int8_t *aac_data, long len, uint32_t timeStamp, aw_faac_config *faac_cfg){
    aw_flv_audio_tag *audio_tag = aw_sw_encoder_create_flv_audio_tag(faac_cfg);
    audio_tag->aac_packet_type = aw_flv_a_aac_package_type_aac_raw;
    
    audio_tag->common_tag.timestamp = timeStamp;
    aw_data *frame_data = alloc_aw_data((uint32_t)len);
    memcpy(frame_data->data, aac_data, len);
    frame_data->size = (uint32_t)len;
    audio_tag->frame_data = frame_data;
    //此处计算的data_size长度为 11(tag header size) + body header size(即下面的header_size，表示body中除去aac data的部分) + aac data size
    audio_tag->common_tag.data_size = audio_tag->frame_data->size + 11 + audio_tag->common_tag.header_size;
    return audio_tag;
}

//创建audio_specific_config_tag
extern aw_flv_audio_tag *aw_encoder_create_audio_specific_config_tag(aw_data *audio_specific_config_data, aw_faac_config *faac_config){
    //创建 audio specfic config record
    aw_flv_audio_tag *audio_tag = aw_sw_encoder_create_flv_audio_tag(faac_config);
    
    //AudioSpecificConfig数据，同正常的audio tag在相同位置
    audio_tag->config_record_data = copy_aw_data(audio_specific_config_data);
    //时间戳0
    audio_tag->common_tag.timestamp = 0;
    //整个tag长度
    audio_tag->common_tag.data_size = audio_specific_config_data->size + 11 + audio_tag->common_tag.header_size;
    
    return audio_tag;
}

