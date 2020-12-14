/*
 copyright 2016 wanghongyu.
 The project page：https://github.com/hardman/AWLive
 My blog page: http://www.jianshu.com/u/1240d2400ca1
 */

#include "aw_x264.h"
#include "aw_alloc.h"
#include "aw_utils.h"
#include <string.h>

static void aw_create_x264_param(aw_x264_context *aw_ctx, x264_param_t ** param){
    if (!param) {
        aw_log("[E] aw_create_h264_param param error");
        return;
    }
    x264_param_t *x264_param = *param;
    if (!x264_param) {
        x264_param = aw_alloc(sizeof(x264_param_t));
        *param = x264_param;
    }
    
    x264_param_default(x264_param);
    x264_param_default_preset(x264_param, "fast" , "zerolatency" );
    
    //视频属性
    x264_param->i_width = aw_ctx->config.width;
    x264_param->i_height = aw_ctx->config.height;
    x264_param->i_frame_total = 0;//编码总帧数
    x264_param->i_keyint_max = 0;//关键帧最大间隔，不用b帧。否则音视频不对齐。还需要对音频做延时处理。
    
    x264_param->i_log_level = X264_LOG_NONE;
    
    //比特率 帧率
    x264_param->rc.i_rc_method = X264_RC_CQP;//CQP(恒定质量)，CRF(恒定码率)，ABR(平均码率)
    x264_param->rc.i_bitrate = (uint32_t)(aw_ctx->config.bitrate / 1000);//1000kbs
    x264_param->rc.i_vbv_max_bitrate = (uint32_t)((aw_ctx->config.bitrate * 1.2) / 1000);
    x264_param->rc.i_vbv_buffer_size = x264_param->rc.i_vbv_max_bitrate;
    x264_param->i_fps_den = 1;
    x264_param->i_fps_num = aw_ctx->config.fps;
    x264_param->i_timebase_den = x264_param->i_fps_num;
    x264_param->i_timebase_num = x264_param->i_fps_den;
}

static void aw_open_x264_handler(aw_x264_context *aw_ctx, x264_param_t *x264_param){
    x264_t *x264_handler = NULL;
    
    int i = 0;
    for (; i < 7; i++) {
        aw_log("x264_profile_names[%d] = %s", i, x264_profile_names[i]);
    }
    
    x264_param_apply_profile(x264_param, x264_profile_names[1]);
    
    x264_handler = x264_encoder_open(x264_param);
    aw_ctx->x264_handler = x264_handler;
    
    if (!x264_handler) {
        aw_log("[E] x264_handler open error");
        return;
    }
}

//保存本次编码的数据
static void aw_x264_save_encoded_data(aw_x264_context *ctx){
    //将编码后的数据转存到encoded_h264_data中，这里面存储的就是编码好的h264视频帧了。
    reset_aw_data(&ctx->encoded_h264_data);
    if (ctx->nal_count > 0) {
        int i = 0;
        for (; i < ctx->nal_count; i++) {
            data_writer.write_bytes(&ctx->encoded_h264_data, ctx->nal[i].p_payload, ctx->nal[i].i_payload);
        }
    }
}

//构造spspps
extern aw_data *aw_create_sps_pps_data(uint8_t *sps_bytes, uint32_t sps_len, uint8_t *pps_bytes, uint32_t pps_len){
    //构造AVCDecoderConfigurationRecord
    //前面6字节固定
    aw_data *avc_decoder_record = alloc_aw_data(0);
    int8_t adr_pre[6] = {0x01, 0x42, 0x00, 0x1e, 0xff, 0xe1};
    data_writer.write_bytes(&avc_decoder_record, (uint8_t *)adr_pre, 6);
    //sps长度
    data_writer.write_uint16(&avc_decoder_record, sps_len);
    data_writer.write_bytes(&avc_decoder_record, (uint8_t *)sps_bytes, sps_len);
    //pps 个数 1
    data_writer.write_uint8(&avc_decoder_record, 1);
    data_writer.write_uint16(&avc_decoder_record, pps_len);
    data_writer.write_bytes(&avc_decoder_record, (uint8_t *)pps_bytes, pps_len);
    return avc_decoder_record;
}

//编码header，sps pps，软编码x264获取sps&pps数据
static void aw_encode_x264_header(aw_x264_context *aw_ctx){
    //主要就是libx264中的此方法
    x264_encoder_headers(aw_ctx->x264_handler, &aw_ctx->nal, &aw_ctx->nal_count);
    
    //将获取到的sps&pps数据取出来，保存到aw_ctx->sps_pps_data中
    //保存sps pps data
    uint8_t *sps_bytes = NULL;
    int8_t sps_len = 0;
    uint8_t *pps_bytes = NULL;
    int8_t pps_len = 0;
    int i = 0;
    for (; i < aw_ctx->nal_count; i++) {
        if (aw_ctx->nal[i].i_type == NAL_SPS) {
            sps_bytes = (uint8_t *)aw_ctx->nal[i].p_payload + 4;
            sps_len = aw_ctx->nal[i].i_payload - 4;
        }else if(aw_ctx->nal[i].i_type == NAL_PPS){
            pps_bytes = (uint8_t *)aw_ctx->nal[i].p_payload + 4;
            pps_len = aw_ctx->nal[i].i_payload - 4;
        }
    }
    
    aw_data *avc_decoder_record = aw_create_sps_pps_data(sps_bytes, sps_len, pps_bytes, pps_len);
    memcpy_aw_data(&aw_ctx->sps_pps_data, avc_decoder_record->data, avc_decoder_record->size);
    free_aw_data(&avc_decoder_record);
}

//编码一帧数据
extern void aw_encode_yuv_frame_2_x264(aw_x264_context *aw_ctx, int8_t *yuv_frame, int len){
    if (len > 0 && yuv_frame) {
        //将视频数据填充到pic_in中，pic_in上面已经介绍过，x264需要这样处理。
        int actual_width = aw_stride(aw_ctx->config.width);
        //数据保存到pic_in中
        if (aw_ctx->config.input_data_format == X264_CSP_NV12) {
            aw_ctx->pic_in->img.plane[0] = (uint8_t *)yuv_frame;
            aw_ctx->pic_in->img.plane[1] = (uint8_t *)yuv_frame + actual_width * aw_ctx->config.height;
        }else if(aw_ctx->config.input_data_format == X264_CSP_BGR || aw_ctx->config.input_data_format == X264_CSP_RGB){
            aw_ctx->pic_in->img.plane[0] = (uint8_t *)yuv_frame;
        }else if(aw_ctx->config.input_data_format == X264_CSP_BGRA){
            aw_ctx->pic_in->img.plane[0] = (uint8_t *)yuv_frame;
        }else{//YUV420
            aw_ctx->pic_in->img.plane[0] = (uint8_t *)yuv_frame;
            aw_ctx->pic_in->img.plane[1] = (uint8_t *)yuv_frame + actual_width * aw_ctx->config.height;
            aw_ctx->pic_in->img.plane[2] = (uint8_t *)yuv_frame + actual_width * aw_ctx->config.height * 5 / 4;
        }
        //x264编码，编码后的数据存储在aw_ctx->nal中
        x264_encoder_encode(aw_ctx->x264_handler, &aw_ctx->nal, &aw_ctx->nal_count, aw_ctx->pic_in, aw_ctx->pic_out);
        aw_ctx->pic_in->i_pts++;
        
        //置空
        aw_ctx->pic_in->img.plane[0] = NULL;
        aw_ctx->pic_in->img.plane[1] = NULL;
        aw_ctx->pic_in->img.plane[2] = NULL;
        aw_ctx->pic_in->img.plane[3] = NULL;
    }
    
    //保存本帧数据
    aw_x264_save_encoded_data(aw_ctx);
}

extern aw_x264_config *alloc_aw_x264_config(){
    aw_x264_config *config = aw_alloc(sizeof(aw_x264_config));
    memset(config, 0, sizeof(aw_x264_config));
    return config;
}

extern void free_aw_x264_config(aw_x264_config **config_p){
    aw_x264_config *config = *config_p;
    if (config) {
        aw_free(config);
        *config_p = NULL;
    }
}

/*
    config 表示配置数据
    aw_x264_context 是自定义结构体，用于存储x264编码重要属性及过程变量。
*/
extern aw_x264_context *alloc_aw_x264_context(aw_x264_config config){
    aw_x264_context *ctx = aw_alloc(sizeof(aw_x264_context));
    memset(ctx, 0, sizeof(aw_x264_context));
    
    //数据默认为 I420
    if (!config.input_data_format) {
        config.input_data_format = X264_CSP_I420;
    }
    
    //创建handler do nothing
    memcpy(&ctx->config, &config, sizeof(aw_x264_config));
    x264_param_t *x264_param = NULL;
    //x264参数，具体请参考：http://blog.csdn.net/table/article/details/8085115
    aw_create_x264_param(ctx, &x264_param);
    //开启编码器
    aw_open_x264_handler(ctx, x264_param);
    aw_free(x264_param);
    
    //创建pic_in，x264内部用于存储输入图像数据的一段空间。
    x264_picture_t *pic_in = aw_alloc(sizeof(x264_picture_t));
    x264_picture_init(pic_in);
    
    //[注意有坑]
    //aw_stride是一个宏，用于将视频宽度转成16的倍数。如果不是16的倍数，有时候会编码失败（颜色缺失等）。
    int alloc_width = aw_stride(config.width);
    
    x264_picture_alloc(pic_in, config.input_data_format, alloc_width, config.height);
    
    pic_in->img.i_csp = config.input_data_format;
    
    //i_stride 表示换行步长，跟plane数及格式有关，x264内部用来判定读取多少数据需要换行。
    //关于yuv数据格式在第二章里面介绍过，这里再次回顾一下。
    if (config.input_data_format == X264_CSP_NV12) {
        //nv12数据包含2个plane，第一个plane存储了y数据大小为 width * height，
        //第二个plane存储uv数据，u和v隔位存储，数据大小为：width * (height / 2)
        pic_in->img.i_stride[0] = alloc_width;
        pic_in->img.i_stride[1] = alloc_width;
        pic_in->img.i_plane = 2;
    }else if(config.input_data_format == X264_CSP_BGR || config.input_data_format == X264_CSP_RGB){
        //rgb数据包含一个plane，数据长度为 width * 3 * height。
        pic_in->img.i_stride[0] = alloc_width * 3;
        pic_in->img.i_plane = 1;
    }else if(config.input_data_format == X264_CSP_BGRA){
        //bgra同rgb类似
        pic_in->img.i_stride[0] = alloc_width * 4;
        pic_in->img.i_plane = 1;
    }else{
        //yuv420即I420格式。
        //包含3个plane，第一个plane存储y数据大小为width * height
        //第二个存储u数据，数据大小为 width * height / 4
        //第三个存储v数据，数据大小为 width * height / 4
        pic_in->img.i_stride[0] = alloc_width;
        pic_in->img.i_stride[1] = alloc_width / 2;
        pic_in->img.i_stride[2] = alloc_width / 2;
        pic_in->img.i_plane = 3;
    }
    
    //其他数据初始化，pic_in 用于存储输入数据(yuv/rgb等数据)，pic_out用于存储输出数据(h264数据)
    ctx->pic_in = pic_in;
    
    ctx->pic_out = aw_alloc(sizeof(x264_picture_t));
    x264_picture_init(ctx->pic_out);
    
    //创建aw_data
    ctx->encoded_h264_data = alloc_aw_data(0);
    ctx->sps_pps_data = alloc_aw_data(0);
    
    //获取sps pps
    // sps pps 数据是rtmp协议要求的必需在所有flv视频帧之前发送的一帧数据，存储了h264视频的一些关键属性。
    // 具体获取方法请看demo，很简单，这里就不解释了。
    aw_encode_x264_header(ctx);
    
    return ctx;
}

/*
    很简单，分别释放pic_in，pic_out，x264_handler即可
*/
extern void free_aw_x264_context(aw_x264_context **ctx_p){
    aw_x264_context *ctx = *ctx_p;
    if (ctx) {
        //释放pic_in
        if (ctx->pic_in) {
            x264_picture_clean(ctx->pic_in);
            aw_free(ctx->pic_in);
            ctx->pic_in = NULL;
        }
        
        //释放pic_out
        if (ctx->pic_out) {
            x264_picture_clean(ctx->pic_out);
            aw_free(ctx->pic_out);
            ctx->pic_out = NULL;
        }
        
        //释放aw_data
        free_aw_data(&ctx->encoded_h264_data);
        
        if (ctx->sps_pps_data) {
            free_aw_data(&ctx->sps_pps_data);
        }
        
        //关闭handler
        if (ctx->x264_handler) {
            x264_encoder_close(ctx->x264_handler);
            ctx->x264_handler = NULL;
        }
        
        aw_free(ctx);
        *ctx_p = NULL;
    }
}

