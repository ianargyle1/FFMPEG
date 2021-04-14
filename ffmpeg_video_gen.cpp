#include<iostream>
#include<fstream>
#include <string>
using namespace std;

extern "C" 
{
#include<libavcodec/avcodec.h>
#include<libavutil/imgutils.h>
#include<libswscale/swscale.h>
#include<libavformat/avformat.h>
#include<libavfilter/avfilter.h>
//#include<libavfilter/avcodec.h>
#include<libavfilter/buffersink.h>
#include<libavfilter/buffersrc.h>
};

AVFrame * open_file (const char * filename)
{
    AVFormatContext *format_ctx = NULL;
    int error_code;
    AVCodec *dec = NULL;
    
    error_code = avformat_open_input(&format_ctx, filename, NULL, NULL);
    if (error_code != 0)
    {
        cout << "Could not open file " << filename << endl;
        return NULL;
    }
    
    error_code = avformat_find_stream_info(format_ctx, NULL);
    if (error_code != 0)
    {
        cout << "Could not find stream info for " << filename << endl;
        return NULL;
    }
    
    AVCodec *codec = NULL;
    dec = avcodec_find_decoder(format_ctx->streams[0]->codec->codec_id);
    if (!dec)
    {
        cout << "Could not find codec for " << filename << endl;
        return NULL;
    }
    
    AVCodecContext * codec_ctx = format_ctx->streams[0]->codec;
    
    error_code = avcodec_open2(codec_ctx, dec, NULL);
    if (error_code != 0)
    {
        cout << "Could not open codec for " << filename << endl;
        return NULL;
    }
    
    AVFrame *frame;
    AVPacket packet;
    
    frame = av_frame_alloc();
    
    av_read_frame(format_ctx, &packet);
    
    int done;
    avcodec_decode_video2(codec_ctx, frame, &done, &packet);
    
    // Convet to RGB
    AVFrame *rgbFrame = av_frame_alloc();
    rgbFrame->width = frame->width;
    rgbFrame->height = frame->height;
    rgbFrame->format = AV_PIX_FMT_RGB24;
    int numBytes = avpicture_get_size(AV_PIX_FMT_RGB24, codec_ctx->width, codec_ctx->height);
    uint8_t *buffer = (uint8_t *) av_malloc(numBytes);
    avpicture_fill((AVPicture*)rgbFrame, buffer, AV_PIX_FMT_RGB24, codec_ctx->width, codec_ctx->height);
    struct SwsContext *sws_ctx;
    sws_ctx = sws_getContext(frame->width, frame->height, (AVPixelFormat) frame->format, frame->width, frame->height, AV_PIX_FMT_RGB24, SWS_BILINEAR, NULL, NULL, NULL);
    sws_scale(sws_ctx, frame->data, frame->linesize, 0, codec_ctx->height, rgbFrame->data, rgbFrame->linesize);
    
    return rgbFrame;
}

AVPacket * encode (AVFrame *frame)
{
    AVCodec * out_codec = avcodec_find_encoder(AV_CODEC_ID_BMP);
    if (out_codec == NULL)
    {
        cout << "Failed to find encoder" << endl;
        return NULL;
    }
    
    AVCodecContext * out_ctx = avcodec_alloc_context3(out_codec);
    if (out_ctx == NULL)
    {
        cout << "Failed to create context" << endl;
        return NULL;
    }
    
    out_ctx->pix_fmt = AV_PIX_FMT_BGR24;
    out_ctx->width = frame->width;
    out_ctx->height = frame->height;
    out_ctx->time_base.num = 1;
    out_ctx->time_base.den = 1;
    
    avcodec_open2(out_ctx, out_codec, NULL);
    
    AVPacket * packet;
    av_init_packet(packet);
    packet->data = NULL;
    packet->size = 0;
    
    int done;
    int size;
    size = avcodec_encode_video2(out_ctx, packet, frame, &done);
    if (size < 0)
    {
        cout << "Encode error" << endl;
        return NULL;
    }
    
    cout << "Encoded" << endl;
    
    return packet;
}
    
int write_image(const char * filename, AVPacket * packet)
{
    ofstream out_file(std::string(filename) + ".bmp");
    out_file.write((const char *)packet->data, packet->size);
    out_file.close();
    return 0;
}

AVFrame * filter_frame(const char * filters_descr, AVFrame * frame)
{
    AVFilterContext *buffersink_ctx;
    AVFilterContext *buffersrc_ctx;
    AVFilterGraph *filter_graph;
    
    char args[512];
    int ret;
    const AVFilter *buffersrc  = avfilter_get_by_name("buffer");
    const AVFilter *buffersink = avfilter_get_by_name("buffersink");
    AVFilterInOut *outputs = avfilter_inout_alloc();
    AVFilterInOut *inputs  = avfilter_inout_alloc();
    enum AVPixelFormat pix_fmts[] = { AV_PIX_FMT_GRAY8, AV_PIX_FMT_NONE };
    filter_graph = avfilter_graph_alloc();
    
    /* buffer video source: the decoded frames from the decoder will be inserted here. */
    snprintf(args, sizeof(args), "%d:%d:%d:%d:%d:%d:%d",
            frame->width, frame->height, frame->format,
            1, 1,
            frame->sample_aspect_ratio.num, frame->sample_aspect_ratio.den);
    ret = avfilter_graph_create_filter(&buffersrc_ctx, buffersrc, "in",
                                        args, NULL, filter_graph);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot create buffer source\n");
        return NULL;
    }
    //AVPixelFormat pix_fmts = AV_PIX_FMT_BGR24;
    /* buffer video sink: to terminate the filter chain. */
    ret = avfilter_graph_create_filter(&buffersink_ctx, buffersink, "out",
                                        NULL, pix_fmts, filter_graph);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot create buffer sink\n");
        return NULL;
    }
    
    /* Endpoints for the filter graph. */
    outputs->name       = av_strdup("in");
    outputs->filter_ctx = buffersrc_ctx;
    outputs->pad_idx    = 0;
    outputs->next       = NULL;
    
    inputs->name       = av_strdup("out");
    inputs->filter_ctx = buffersink_ctx;
    inputs->pad_idx    = 0;
    inputs->next       = NULL;
    
    if ((ret = avfilter_graph_parse(filter_graph, filters_descr,
                                    inputs, outputs, NULL)) < 0)
        return NULL;
    
    if ((ret = avfilter_graph_config(filter_graph, NULL)) < 0)
        return NULL;
        
        
    AVFrame *picref;
    picref = av_frame_alloc();
    /* push the decoded frame into the filtergraph */
    av_buffersrc_add_frame(buffersrc_ctx, frame);
    av_buffersink_get_frame(buffersink_ctx, picref);
    return picref;
    //return frame;
 }

int main()
{
    AVFrame *frame;
    frame = open_file("./test_img2.png");
    if (frame == NULL)
    {
        cout << "Bailing." << endl;
    }
    // cout << frame->width << endl;
    // cout << frame->height << endl;
    // int x = 10, y = 10, b;
    // for (b = 0; b < 3; b++)
    //     cout << (int) (frame->data[0][y*frame->linesize[0]+x*3+b]) << endl;
        
    // string str = "scale=78:24,transpose=cclock";
    // const char * c = str.c_str();
    //AVFrame copy = *frame;
    //AVFrame * c = filter_frame("scale=78:24,transpose=cclock", frame);
    //AVPacket * p = 
    cout << frame->width << endl;
    frame = filter_frame("scale=50:50", frame);
    cout << frame->width << endl;
    AVPacket * p = encode(frame);
    write_image("./test_img", p);
    cout<<"Hello World!"<< endl;
    return 0;
}
