//
//  H264_Save.c
//  iFrameExtractor
//
//  Created by Liao KuoHsun on 13/5/24.
//
//

// Reference ffmpeg\doc\examples\muxing.c
#include <stdio.h>
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "Mp4_Save.h"
//#include "libavformat/avio.h"
//#import "AudioUtilities.h"

int vVideoStreamIdx = -1, vAudioStreamIdx = -1,  waitkey = 1;




// Reference : https://github.com/mstorsjo/libav/blob/fdk-aac/libavcodec/aacadtsdec.c
typedef struct AACADTSHeaderInfo {
    
    // == adts_fixed_header ==
    uint16_t   syncword;                // 12 bslbf
    uint8_t    ID;                       // 1 bslbf
    uint8_t    layer;                    // 2 uimsbf
    uint8_t    protection_absent;        // 1 bslbf
    uint8_t    profile;                  // 2 uimsbf
    uint8_t    sampling_frequency_index; // 4 uimsbf
    uint8_t    private_bit;              // 1 bslbf
    uint8_t    channel_configuration;    // 3 uimsbf
    uint8_t    original_copy;            // 1 bslbf
    uint8_t    home;                     // 1 bslbf
    
    // == adts_variable_header ==
    uint8_t copyright_identification_bit; //1 bslbf
    uint8_t copyright_identification_start; //1 bslbf
    uint16_t frame_length; //13 bslbf
    uint16_t adts_buffer_fullness; //11 bslbf
    uint8_t number_of_raw_data_blocks_in_frame; //2 uimsfb
    
} tAACADTSHeaderInfo;

int parseAACADTSHeader(unsigned char *pInput,tAACADTSHeaderInfo *pADTSHeader)
{
    int bHasSyncword = 0;
    if(pADTSHeader==NULL)
        return 0;
    
    // == adts_fixed_header ==
    //   syncword; 12 bslbf should be 0x1111 1111 1111
    if(pInput[0]==0xFF)
    {
        if((pInput[1]&0xF0)==0xF0)
        {
            bHasSyncword = 1;
        }
    }
    
    if(!bHasSyncword) return 0;
    
    //== adts_fixed_header ==
    //    uint16_t   syncword;                // 12 bslbf
    //    uint8_t    ID;                       // 1 bslbf
    //    uint8_t    layer;                    // 2 uimsbf
    //    uint8_t    protection_absent;        // 1 bslbf
    //    uint8_t    profile;                  // 2 uimsbf
    //    uint8_t    sampling_frequency_index; // 4 uimsbf
    //    uint8_t    private_bit;              // 1 bslbf
    //    uint8_t    channel_configuration;    // 3 uimsbf
    //    uint8_t    original_copy;            // 1 bslbf
    //    uint8_t    home;                     // 1 bslbf
    
    pADTSHeader->syncword = 0x0fff;
    pADTSHeader->ID = (pInput[1]&0x08)>>3;
    pADTSHeader->layer = (pInput[1]&0x06)>>2;
    pADTSHeader->protection_absent = pInput[1]&0x01;
    
    pADTSHeader->profile = (pInput[2]&0xC0)>>6;
    pADTSHeader->sampling_frequency_index = (pInput[2]&0x3C)>>2;
    pADTSHeader->private_bit = (pInput[2]&0x02)>>1;
    
    pADTSHeader->channel_configuration = ((pInput[2]&0x01)<<2) + ((pInput[3]&0xC0)>>6);
    pADTSHeader->original_copy = ((pInput[3]&0x20)>>5);
    pADTSHeader->home = ((pInput[3]&0x10)>>4);
    
    
    // == adts_variable_header ==
    //    copyright_identification_bit; 1 bslbf
    //    copyright_identification_start; 1 bslbf
    //    frame_length; 13 bslbf
    //    adts_buffer_fullness; 11 bslbf
    //    number_of_raw_data_blocks_in_frame; 2 uimsfb
    
    pADTSHeader->copyright_identification_bit = ((pInput[3]&0x08)>>3);
    pADTSHeader->copyright_identification_start = ((pInput[3]&0x04)>>2);
    pADTSHeader->frame_length = ((pInput[3]&0x03)<<11) + ((pInput[4])<<3) + ((pInput[5]&0xE0)>>5);
    pADTSHeader->adts_buffer_fullness = ((pInput[5]&0x1F)<<6) + ((pInput[6]&0xFC)>>2);
    pADTSHeader->number_of_raw_data_blocks_in_frame = ((pInput[6]&0x03));
    
    
    // We can't use bits mask to convert byte array to ADTS structure.
    // http://mjfrazer.org/mjfrazer/bitfields/
    // Big endian machines pack bitfields from most significant byte to least.
    // Little endian machines pack bitfields from least significant byte to most.
    // Direct bits mapping is hard....  we should implement a parser ourself.
    
    return 1;
}


// < 0 = error
// 0 = I-Frame
// 1 = P-Frame
// 2 = B-Frame
// 3 = S-Frame
static int getVopType( const void *p, int len )
{
    
    if ( !p || 6 >= len )
    {
        fprintf(stderr, "getVopType() error");
        return -1;
    }
    
    unsigned char *b = (unsigned char*)p;
    
    // Verify VOP id
    if ( 0xb6 == *b )
    {
        b++;
        return ( *b & 0xc0 ) >> 6;
    } // end if
    
    switch( *b )
    {
        case 0x65 : return 0;
        case 0x61 : return 1;
        case 0x01 : return 2;
    } // end switch
    
    return -1;
}

void h264_file_close(AVFormatContext *fc)
{
    if ( !fc )
        return;
    
    av_write_trailer( fc );
    
    if ( fc->oformat && !( fc->oformat->flags & AVFMT_NOFILE ) && fc->pb )
        avio_close( fc->pb );
    
    av_free( fc );
}



// Since the data may not from ffmpeg as AVPacket format
void h264_file_write_frame(AVFormatContext *fc, int vStreamIdx, const void* p, int len, int64_t dts, int64_t pts )
{
    AVStream *pst = NULL;
    AVPacket pkt;
    
    if ( 0 > vVideoStreamIdx )
        return;

    // may be audio or video
    pst = fc->streams[ vStreamIdx ];
    
    // Init packet
    av_init_packet( &pkt );
    
    if(vStreamIdx ==vVideoStreamIdx)
    {
        pkt.flags |= ( 0 >= getVopType( p, len ) ) ? AV_PKT_FLAG_KEY : 0;
        //pkt.flags |= AV_PKT_FLAG_KEY;
        pkt.stream_index = pst->index;
        pkt.data = (uint8_t*)p;
        pkt.size = len;
    
#if PTS_DTS_IS_CORRECT == 1
        pkt.dts = dts;
        pkt.pts = pts;
#else
        pkt.dts = AV_NOPTS_VALUE;
        pkt.pts = AV_NOPTS_VALUE;
#endif
        // TODO: mark or unmark the log
        //fprintf(stderr, "dts=%lld, pts=%lld\n",dts,pts);
        // av_write_frame( fc, &pkt );
    }
    av_interleaved_write_frame( fc, &pkt );
}

void h264_file_write_audio_frame(AVFormatContext *fc, AVCodecContext *pAudioCodecContext ,int vStreamIdx, const void* pData, int vDataLen, int64_t dts, int64_t pts )
{
    int vRet=0;
    AVCodecContext *pAudioOutputCodecContext;
    AVStream *pst = NULL;
    AVPacket pkt;
    
    if ( 0 > vVideoStreamIdx )
        return;
    
    // may be audio or video
    pst = fc->streams[ vStreamIdx ];
    pAudioOutputCodecContext = pst->codec;
    
    // Init packet
    av_init_packet( &pkt );
    
    if(vStreamIdx==vAudioStreamIdx)
    {
        if(pAudioOutputCodecContext==NULL)
        {
            fprintf(stderr,"pAudioOutputCodecContext==NULL");
        }
        else
        {
            int bIsADTSAAS=0, vRedudantHeaderOfAAC=0;
            AVPacket AudioPacket={0};
            tAACADTSHeaderInfo vxADTSHeader={0};
            uint8_t *pHeader = (uint8_t *)pData;
            
            //bIsADTSAAS = [AudioUtilities parseAACADTSHeader:pHeader ToHeader:(tAACADTSHeaderInfo *) &vxADTSHeader];
            bIsADTSAAS = parseAACADTSHeader(pHeader, &vxADTSHeader);
            // If header has the syncword of adts_fixed_header
            // syncword = 0xFFF
            if(bIsADTSAAS)
            {
                vRedudantHeaderOfAAC = 7;
            }
            else
            {
                vRedudantHeaderOfAAC = 0;
            }
            
#if 0
            int gotFrame=0, len=0;
            
            AVFrame vxAVFrame1={0};
            AVFrame *pAVFrame1 = &vxAVFrame1;
            
            av_init_packet(&AudioPacket);
            avcodec_get_frame_defaults(pAVFrame1);

            if(bIsADTSAAS)
            {
                AudioPacket.size = vDataLen-vRedudantHeaderOfAAC;
                AudioPacket.data = pHeader+vRedudantHeaderOfAAC;
            }
            else
            {
                // This will produce error message
                // "malformated aac bitstream, use -absf aac_adtstoasc"
                AudioPacket.size = vDataLen;
                AudioPacket.data = pHeader;
            }
            // Decode from input format to PCM
            len = avcodec_decode_audio4(pAudioCodecContext, pAVFrame1, &gotFrame, &AudioPacket);
            
            // Encode from PCM to AAC
            vRet = avcodec_encode_audio2(pAudioOutputCodecContext, &pkt, pAVFrame1, &gotFrame);
            if(vRet!=0)
                NSLog(@"avcodec_encode_audio2 fail");
            pkt.stream_index = vStreamIdx;//pst->index;

#else

            //if(pAudioCodecContext->codec_id==AV_CODEC_ID_AAC)
            {
                // This will produce error message
                // "malformated aac bitstream, use -absf aac_adtstoasc"
                pkt.size = vDataLen-vRedudantHeaderOfAAC;
                pkt.data = pHeader+vRedudantHeaderOfAAC;
                pkt.stream_index = vStreamIdx;//pst->index;
                pkt.flags |= AV_PKT_FLAG_KEY;
                
            }

#endif
//            pkt.dts = AV_NOPTS_VALUE;
//            pkt.pts = AV_NOPTS_VALUE;
            vRet = av_interleaved_write_frame( fc, &pkt );
            if(vRet!=0)
                fprintf(stderr,"av_interleaved_write_frame for audio fail");
        }
    }


}


void h264_file_write_frame2(AVFormatContext *fc, int vStreamIdx, AVPacket *pPkt )
{    
    av_interleaved_write_frame( fc, pPkt );
}


int h264_file_create(const char *pFilePath, AVFormatContext *fc, AVCodecContext *pCodecCtx,AVCodecContext *pAudioCodecCtx, double fps, void *p, int len )
{
    int vRet=0;
    AVOutputFormat *of=NULL;
    AVStream *pst=NULL;
    AVCodecContext *pcc=NULL, *pAudioOutputCodecContext=NULL;

    avcodec_register_all();
    av_register_all();
    av_log_set_level(AV_LOG_VERBOSE);
    
    if(!pFilePath)
    {
        fprintf(stderr, "FilePath no exist");
        return -1;
    }
    
    if(!fc)
    {
        fprintf(stderr, "AVFormatContext no exist");
        return -1;
    }
    fprintf(stderr, "file=%s\n",pFilePath);
    
    // Create container
    of = av_guess_format( 0, pFilePath, 0 );
    fc->oformat = of;
    strcpy( fc->filename, pFilePath );
    
    // Add video stream
    pst = avformat_new_stream( fc, 0 );
    vVideoStreamIdx = pst->index;
    fprintf(stderr,"Video Stream:%d",vVideoStreamIdx);
    
    pcc = pst->codec;
    avcodec_get_context_defaults3( pcc, AVMEDIA_TYPE_VIDEO );

    // TODO: test here
    //*pcc = *pCodecCtx;
    
    // TODO: check ffmpeg source for "q=%d-%d", some parameter should be set before write header
    
    // Save the stream as origin setting without convert
    pcc->codec_type = pCodecCtx->codec_type;
    pcc->codec_id = pCodecCtx->codec_id;
    pcc->bit_rate = pCodecCtx->bit_rate;
    pcc->width = pCodecCtx->width;
    pcc->height = pCodecCtx->height;
    
#if PTS_DTS_IS_CORRECT == 1
    pcc->time_base.num = pCodecCtx->time_base.num;
    pcc->time_base.den = pCodecCtx->time_base.den;
    pcc->ticks_per_frame = pCodecCtx->ticks_per_frame;
//    pcc->frame_bits= pCodecCtx->frame_bits;
//    pcc->frame_size= pCodecCtx->frame_size;
//    pcc->frame_number= pCodecCtx->frame_number;
    
//    pcc->pts_correction_last_dts = pCodecCtx->pts_correction_last_dts;
//    pcc->pts_correction_last_pts = pCodecCtx->pts_correction_last_pts;
    
    NSLog(@"time_base, num=%d, den=%d, fps should be %g",\
          pcc->time_base.num, pcc->time_base.den, \
          (1.0/ av_q2d(pCodecCtx->time_base)/pcc->ticks_per_frame));
#else
    if(fps==0)
    {
        double fps=0.0;
        AVRational pTimeBase;
        pTimeBase.num = pCodecCtx->time_base.num;
        pTimeBase.den = pCodecCtx->time_base.den;
        fps = 1.0/ av_q2d(pCodecCtx->time_base)/ FFMAX(pCodecCtx->ticks_per_frame, 1);
        fprintf(stderr,"fps_method(tbc): 1/av_q2d()=%g",fps);
        pcc->time_base.num = 1;
        pcc->time_base.den = fps;
    }
    else
    {
        pcc->time_base.num = 1;
        pcc->time_base.den = fps;
    }
#endif
    // reference ffmpeg\libavformat\utils.c

    // For SPS and PPS in avcC container
    pcc->extradata = malloc(sizeof(uint8_t)*pCodecCtx->extradata_size);
    memcpy(pcc->extradata, pCodecCtx->extradata, pCodecCtx->extradata_size);
    pcc->extradata_size = pCodecCtx->extradata_size;
    
    // For Audio stream
    if(pAudioCodecCtx)
    {
        AVCodec *pAudioCodec=NULL;
        AVStream *pst2=NULL;
        pAudioCodec = avcodec_find_encoder(AV_CODEC_ID_AAC);
        
        // Add audio stream
        pst2 = avformat_new_stream( fc, pAudioCodec );
        vAudioStreamIdx = pst2->index;
        pAudioOutputCodecContext = pst2->codec;
        avcodec_get_context_defaults3( pAudioOutputCodecContext, pAudioCodec );
        fprintf(stderr,"Audio Stream:%d",vAudioStreamIdx);
        fprintf(stderr,"pAudioCodecCtx->bits_per_coded_sample=%d",pAudioCodecCtx->bits_per_coded_sample);
        
        pAudioOutputCodecContext->codec_type = AVMEDIA_TYPE_AUDIO;
        pAudioOutputCodecContext->codec_id = AV_CODEC_ID_AAC;
        
        // Copy the codec attributes
        pAudioOutputCodecContext->channels = pAudioCodecCtx->channels;
        pAudioOutputCodecContext->channel_layout = pAudioCodecCtx->channel_layout;
        pAudioOutputCodecContext->sample_rate = pAudioCodecCtx->sample_rate;
        pAudioOutputCodecContext->bit_rate = 12000;//pAudioCodecCtx->sample_rate * pAudioCodecCtx->bits_per_coded_sample;
        pAudioOutputCodecContext->bits_per_coded_sample = pAudioCodecCtx->bits_per_coded_sample;
        pAudioOutputCodecContext->profile = pAudioCodecCtx->profile;
        //FF_PROFILE_AAC_LOW;
        // pAudioCodecCtx->bit_rate;
        
        // AV_SAMPLE_FMT_U8P, AV_SAMPLE_FMT_S16P
        //pAudioOutputCodecContext->sample_fmt = AV_SAMPLE_FMT_FLTP;//pAudioCodecCtx->sample_fmt;
        pAudioOutputCodecContext->sample_fmt = pAudioCodecCtx->sample_fmt;
        //pAudioOutputCodecContext->sample_fmt = AV_SAMPLE_FMT_U8;
        
        pAudioOutputCodecContext->sample_aspect_ratio = pAudioCodecCtx->sample_aspect_ratio;

        pAudioOutputCodecContext->time_base.num = pAudioCodecCtx->time_base.num;
        pAudioOutputCodecContext->time_base.den = pAudioCodecCtx->time_base.den;
        pAudioOutputCodecContext->ticks_per_frame = pAudioCodecCtx->ticks_per_frame;
        pAudioOutputCodecContext->frame_size = 1024;
        
        fprintf(stderr,"profile:%d, sample_rate:%d, channles:%d", pAudioOutputCodecContext->profile, pAudioOutputCodecContext->sample_rate, pAudioOutputCodecContext->channels);
        AVDictionary *opts = NULL;
        av_dict_set(&opts, "strict", "experimental", 0);
        
        if (avcodec_open2(pAudioOutputCodecContext, pAudioCodec, &opts) < 0) {
            fprintf(stderr, "\ncould not open codec\n");
        }
        
        av_dict_free(&opts);
        
#if 0
        // For Audio, this part is no need
        if(pAudioCodecCtx->extradata_size!=0)
        {
            NSLog(@"extradata_size !=0");
            pAudioOutputCodecContext->extradata = malloc(sizeof(uint8_t)*pAudioCodecCtx->extradata_size);
            memcpy(pAudioOutputCodecContext->extradata, pAudioCodecCtx->extradata, pAudioCodecCtx->extradata_size);
            pAudioOutputCodecContext->extradata_size = pAudioCodecCtx->extradata_size;
        }
        else
        {
            // For WMA test only
            pAudioOutputCodecContext->extradata_size = 0;
            NSLog(@"extradata_size ==0");
        }
#endif
    }
    
    if(fc->oformat->flags & AVFMT_GLOBALHEADER)
    {
        pcc->flags |= CODEC_FLAG_GLOBAL_HEADER;
        if (pAudioOutputCodecContext) {
            pAudioOutputCodecContext->flags |= CODEC_FLAG_GLOBAL_HEADER;
        }
    }
    
    if ( !( fc->oformat->flags & AVFMT_NOFILE ) )
    {
        vRet = avio_open( &fc->pb, fc->filename, AVIO_FLAG_WRITE );
        if(vRet!=0)
        {
            fprintf(stderr,"avio_open(%s) error", fc->filename);
        }
    }
    
    // dump format in console
    av_dump_format(fc, 0, pFilePath, 1);
    
    vRet = avformat_write_header( fc, NULL );
    if(vRet==0)
        return 1;
    else
        return 0;
}




/*
 * qt-faststart.c, v0.2
 * by Mike Melanson (melanson@pcisys.net)
 * This file is placed in the public domain. Use the program however you
 * see fit.
 *
 * This utility rearranges a Quicktime file such that the moov atom
 * is in front of the data, thus facilitating network streaming.
 *
 * To compile this program, start from the base directory from which you
 * are building FFmpeg and type:
 *  make tools/qt-faststart
 * The qt-faststart program will be built in the tools/ directory. If you
 * do not build the program in this manner, correct results are not
 * guaranteed, particularly on 64-bit platforms.
 * Invoke the program with:
 *  qt-faststart <infile.mov> <outfile.mov>
 *
 * Notes: Quicktime files can come in many configurations of top-level
 * atoms. This utility stipulates that the very last atom in the file needs
 * to be a moov atom. When given such a file, this utility will rearrange
 * the top-level atoms by shifting the moov atom from the back of the file
 * to the front, and patch the chunk offsets along the way. This utility
 * presently only operates on uncompressed moov atoms.
 */

#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <string.h>

#ifdef __MINGW32__
#define fseeko(x, y, z) fseeko64(x, y, z)
#define ftello(x)       ftello64(x)
#elif defined(_WIN32)
#define fseeko(x, y, z) _fseeki64(x, y, z)
#define ftello(x)       _ftelli64(x)
#endif

#define FFMIN(a,b) ((a) > (b) ? (b) : (a))

#define BE_16(x) ((((uint8_t*)(x))[0] <<  8) | ((uint8_t*)(x))[1])

#define BE_32(x) ((((uint8_t*)(x))[0] << 24) |  \
(((uint8_t*)(x))[1] << 16) |  \
(((uint8_t*)(x))[2] <<  8) |  \
((uint8_t*)(x))[3])

#define BE_64(x) (((uint64_t)(((uint8_t*)(x))[0]) << 56) |  \
((uint64_t)(((uint8_t*)(x))[1]) << 48) |  \
((uint64_t)(((uint8_t*)(x))[2]) << 40) |  \
((uint64_t)(((uint8_t*)(x))[3]) << 32) |  \
((uint64_t)(((uint8_t*)(x))[4]) << 24) |  \
((uint64_t)(((uint8_t*)(x))[5]) << 16) |  \
((uint64_t)(((uint8_t*)(x))[6]) <<  8) |  \
((uint64_t)( (uint8_t*)(x))[7]))

#define BE_FOURCC(ch0, ch1, ch2, ch3)           \
( (uint32_t)(unsigned char)(ch3)        |   \
((uint32_t)(unsigned char)(ch2) <<  8) |   \
((uint32_t)(unsigned char)(ch1) << 16) |   \
((uint32_t)(unsigned char)(ch0) << 24) )

#define QT_ATOM BE_FOURCC
/* top level atoms */
#define FREE_ATOM QT_ATOM('f', 'r', 'e', 'e')
#define JUNK_ATOM QT_ATOM('j', 'u', 'n', 'k')
#define MDAT_ATOM QT_ATOM('m', 'd', 'a', 't')
#define MOOV_ATOM QT_ATOM('m', 'o', 'o', 'v')
#define PNOT_ATOM QT_ATOM('p', 'n', 'o', 't')
#define SKIP_ATOM QT_ATOM('s', 'k', 'i', 'p')
#define WIDE_ATOM QT_ATOM('w', 'i', 'd', 'e')
#define PICT_ATOM QT_ATOM('P', 'I', 'C', 'T')
#define FTYP_ATOM QT_ATOM('f', 't', 'y', 'p')
#define UUID_ATOM QT_ATOM('u', 'u', 'i', 'd')

#define CMOV_ATOM QT_ATOM('c', 'm', 'o', 'v')
#define STCO_ATOM QT_ATOM('s', 't', 'c', 'o')
#define CO64_ATOM QT_ATOM('c', 'o', '6', '4')

#define ATOM_PREAMBLE_SIZE    8
#define COPY_BUFFER_SIZE   33554432


//int main(int argc, char *argv[])
int MoveMP4MoovToHeader(char *pSrc, char *pDst)
{
    FILE *infile  = NULL;
    FILE *outfile = NULL;
    unsigned char atom_bytes[ATOM_PREAMBLE_SIZE];
    uint32_t atom_type   = 0;
    uint64_t atom_size   = 0;
    uint64_t atom_offset = 0;
    uint64_t last_offset;
    unsigned char *moov_atom = NULL;
    unsigned char *ftyp_atom = NULL;
    uint64_t moov_atom_size;
    uint64_t ftyp_atom_size = 0;
    uint64_t i, j;
    uint32_t offset_count;
    uint64_t current_offset;
    int64_t start_offset = 0;
    unsigned char *copy_buffer = NULL;
    int bytes_to_copy;
    
    if((pSrc==NULL) || (pDst==NULL)) return 0 ;
    
    //    if (argc != 3) {
    //        printf("Usage: qt-faststart <infile.mov> <outfile.mov>\n");
    //        return 0;
    //    }
    //
    //    if (!strcmp(argv[1], argv[2])) {
    //        fprintf(stderr, "input and output files need to be different\n");
    //        return 1;
    //    }
    
    infile = fopen(pSrc, "rb");
    if (!infile) {
        perror(pSrc);
        goto error_out;
    }
    
    /* traverse through the atoms in the file to make sure that 'moov' is
     * at the end */
    while (!feof(infile)) {
        if (fread(atom_bytes, ATOM_PREAMBLE_SIZE, 1, infile) != 1) {
            break;
        }
        atom_size = (uint32_t) BE_32(&atom_bytes[0]);
        atom_type = BE_32(&atom_bytes[4]);
        
        /* keep ftyp atom */
        if (atom_type == FTYP_ATOM) {
            ftyp_atom_size = atom_size;
            free(ftyp_atom);
            ftyp_atom = malloc(ftyp_atom_size);
            if (!ftyp_atom) {
                printf("could not allocate %"PRIu64" bytes for ftyp atom\n",
                       atom_size);
                goto error_out;
            }
            if (   fseeko(infile, -ATOM_PREAMBLE_SIZE, SEEK_CUR)
                || fread(ftyp_atom, atom_size, 1, infile) != 1
                || (start_offset = ftello(infile))<0) {
                perror(pSrc);
                goto error_out;
            }
        } else {
            int ret;
            /* 64-bit special case */
            if (atom_size == 1) {
                if (fread(atom_bytes, ATOM_PREAMBLE_SIZE, 1, infile) != 1) {
                    break;
                }
                atom_size = BE_64(&atom_bytes[0]);
                ret = fseeko(infile, atom_size - ATOM_PREAMBLE_SIZE * 2, SEEK_CUR);
            } else {
                ret = fseeko(infile, atom_size - ATOM_PREAMBLE_SIZE, SEEK_CUR);
            }
            if(ret) {
                perror(pSrc);
                goto error_out;
            }
        }
        printf("%c%c%c%c %10"PRIu64" %"PRIu64"\n",
               (atom_type >> 24) & 255,
               (atom_type >> 16) & 255,
               (atom_type >>  8) & 255,
               (atom_type >>  0) & 255,
               atom_offset,
               atom_size);
        if ((atom_type != FREE_ATOM) &&
            (atom_type != JUNK_ATOM) &&
            (atom_type != MDAT_ATOM) &&
            (atom_type != MOOV_ATOM) &&
            (atom_type != PNOT_ATOM) &&
            (atom_type != SKIP_ATOM) &&
            (atom_type != WIDE_ATOM) &&
            (atom_type != PICT_ATOM) &&
            (atom_type != UUID_ATOM) &&
            (atom_type != FTYP_ATOM)) {
            printf("encountered non-QT top-level atom (is this a QuickTime file?)\n");
            break;
        }
        atom_offset += atom_size;
        
        /* The atom header is 8 (or 16 bytes), if the atom size (which
         * includes these 8 or 16 bytes) is less than that, we won't be
         * able to continue scanning sensibly after this atom, so break. */
        if (atom_size < 8)
            break;
    }
    
    if (atom_type != MOOV_ATOM) {
        printf("last atom in file was not a moov atom\n");
        free(ftyp_atom);
        fclose(infile);
        return 0;
    }
    
    /* moov atom was, in fact, the last atom in the chunk; load the whole
     * moov atom */
    if (fseeko(infile, -atom_size, SEEK_END)) {
        perror(pSrc);
        goto error_out;
    }
    last_offset    = ftello(infile);
    moov_atom_size = atom_size;
    moov_atom      = malloc(moov_atom_size);
    if (!moov_atom) {
        printf("could not allocate %"PRIu64" bytes for moov atom\n", atom_size);
        goto error_out;
    }
    if (fread(moov_atom, atom_size, 1, infile) != 1) {
        perror(pSrc);
        goto error_out;
    }
    
    /* this utility does not support compressed atoms yet, so disqualify
     * files with compressed QT atoms */
    if (BE_32(&moov_atom[12]) == CMOV_ATOM) {
        printf("this utility does not support compressed moov atoms yet\n");
        goto error_out;
    }
    
    /* close; will be re-opened later */
    fclose(infile);
    infile = NULL;
    
    /* crawl through the moov chunk in search of stco or co64 atoms */
    for (i = 4; i < moov_atom_size - 4; i++) {
        atom_type = BE_32(&moov_atom[i]);
        if (atom_type == STCO_ATOM) {
            printf(" patching stco atom...\n");
            atom_size = (uint32_t)BE_32(&moov_atom[i - 4]);
            if (i + atom_size - 4 > moov_atom_size) {
                printf(" bad atom size\n");
                goto error_out;
            }
            offset_count = BE_32(&moov_atom[i + 8]);
            if (i + 12LL + offset_count * 4LL > moov_atom_size) {
                printf(" bad atom size\n");
                goto error_out;
            }
            for (j = 0; j < offset_count; j++) {
                current_offset  = (uint32_t)BE_32(&moov_atom[i + 12 + j * 4]);
                current_offset += moov_atom_size;
                moov_atom[i + 12 + j * 4 + 0] = (current_offset >> 24) & 0xFF;
                moov_atom[i + 12 + j * 4 + 1] = (current_offset >> 16) & 0xFF;
                moov_atom[i + 12 + j * 4 + 2] = (current_offset >>  8) & 0xFF;
                moov_atom[i + 12 + j * 4 + 3] = (current_offset >>  0) & 0xFF;
            }
            i += atom_size - 4;
        } else if (atom_type == CO64_ATOM) {
            printf(" patching co64 atom...\n");
            atom_size = (uint32_t)BE_32(&moov_atom[i - 4]);
            if (i + atom_size - 4 > moov_atom_size) {
                printf(" bad atom size\n");
                goto error_out;
            }
            offset_count = BE_32(&moov_atom[i + 8]);
            if (i + 12LL + offset_count * 8LL > moov_atom_size) {
                printf(" bad atom size\n");
                goto error_out;
            }
            for (j = 0; j < offset_count; j++) {
                current_offset  = BE_64(&moov_atom[i + 12 + j * 8]);
                current_offset += moov_atom_size;
                moov_atom[i + 12 + j * 8 + 0] = (current_offset >> 56) & 0xFF;
                moov_atom[i + 12 + j * 8 + 1] = (current_offset >> 48) & 0xFF;
                moov_atom[i + 12 + j * 8 + 2] = (current_offset >> 40) & 0xFF;
                moov_atom[i + 12 + j * 8 + 3] = (current_offset >> 32) & 0xFF;
                moov_atom[i + 12 + j * 8 + 4] = (current_offset >> 24) & 0xFF;
                moov_atom[i + 12 + j * 8 + 5] = (current_offset >> 16) & 0xFF;
                moov_atom[i + 12 + j * 8 + 6] = (current_offset >>  8) & 0xFF;
                moov_atom[i + 12 + j * 8 + 7] = (current_offset >>  0) & 0xFF;
            }
            i += atom_size - 4;
        }
    }
    
    /* re-open the input file and open the output file */
    infile = fopen(pSrc, "rb");
    if (!infile) {
        perror(pSrc);
        goto error_out;
    }
    
    if (start_offset > 0) { /* seek after ftyp atom */
        if (fseeko(infile, start_offset, SEEK_SET)) {
            perror(pSrc);
            goto error_out;
        }
        
        last_offset -= start_offset;
    }
    
    outfile = fopen(pDst, "wb");
    if (!outfile) {
        perror(pDst);
        goto error_out;
    }
    
    /* dump the same ftyp atom */
    if (ftyp_atom_size > 0) {
        printf(" writing ftyp atom...\n");
        if (fwrite(ftyp_atom, ftyp_atom_size, 1, outfile) != 1) {
            perror(pDst);
            goto error_out;
        }
    }
    
    /* dump the new moov atom */
    printf(" writing moov atom...\n");
    if (fwrite(moov_atom, moov_atom_size, 1, outfile) != 1) {
        perror(pDst);
        goto error_out;
    }
    
    /* copy the remainder of the infile, from offset 0 -> last_offset - 1 */
    bytes_to_copy = FFMIN(COPY_BUFFER_SIZE, last_offset);
    copy_buffer = malloc(bytes_to_copy);
    if (!copy_buffer) {
        printf("could not allocate %d bytes for copy_buffer\n", bytes_to_copy);
        goto error_out;
    }
    printf(" copying rest of file...\n");
    while (last_offset) {
        bytes_to_copy = FFMIN(bytes_to_copy, last_offset);
        
        if (fread(copy_buffer, bytes_to_copy, 1, infile) != 1) {
            perror(pSrc);
            goto error_out;
        }
        if (fwrite(copy_buffer, bytes_to_copy, 1, outfile) != 1) {
            perror(pDst);
            goto error_out;
        }
        last_offset -= bytes_to_copy;
    }
    
    fclose(infile);
    fclose(outfile);
    free(moov_atom);
    free(ftyp_atom);
    free(copy_buffer);
    
    return 0;
    
error_out:
    if (infile)
        fclose(infile);
    if (outfile)
        fclose(outfile);
    free(moov_atom);
    free(ftyp_atom);
    free(copy_buffer);
    return 1;
}


