
#include <chiaki/ffmpegdecoder.h>
#include <chiaki/time.h>
#include <libavcodec/avcodec.h>
#include <libavutil/pixdesc.h>
#if defined(_WIN32)
#include <libavutil/hwcontext_d3d11va.h>
#endif
#include <math.h>

static enum AVCodecID chiaki_codec_av_codec_id(ChiakiCodec codec)
{
	switch(codec)
	{
		case CHIAKI_CODEC_H265:
		case CHIAKI_CODEC_H265_HDR:
			return AV_CODEC_ID_H265;
		default:
			return AV_CODEC_ID_H264;
	}
}

static double chiaki_ffmpeg_decoder_default_frame_duration_us(unsigned int max_fps)
{
	double fps = max_fps > 0 ? (double)max_fps : 60.0;
	if(fps <= 0.0)
		fps = 60.0;
	return 1000000.0 / fps;
}

#if defined(_WIN32)
// d3d11va decodes into an NV12/P010 texture array owned by the frames context.
// The GUI renders those textures zero-copy through pl_d3d11_wrap(), which only
// marks a wrapped texture as sampleable when it was created with
// D3D11_BIND_SHADER_RESOURCE. ffmpeg's d3d11va backend uses
// AVD3D11VAFramesContext.BindFlags verbatim (no default, nothing OR-ed in), so
// the flag has to be requested explicitly. Without it pl_render_image() rejects
// every decoded frame ("texture->params.sampleable" validation failure in
// renderer.c) and the stream shows nothing but a black window.
//
// A frames context must exist before the first decoded frame can be allocated,
// and its dimensions are only known once the decoder has parsed the stream's
// sequence header - which is exactly when get_format() runs. Derive the
// standard parameters from avcodec_get_hw_frames_parameters() (the same helper
// libavcodec uses internally) and then add the shader-resource bit, so the pool
// size, pixel format and dimensions keep matching what the decoder expects.
static enum AVPixelFormat chiaki_ffmpeg_get_hw_format(AVCodecContext *ctx, const enum AVPixelFormat *pix_fmts)
{
	ChiakiFfmpegDecoder *decoder = ctx->opaque;
	for(const enum AVPixelFormat *p = pix_fmts; *p != AV_PIX_FMT_NONE; p++)
	{
		if(*p != decoder->hw_pix_fmt)
			continue;

		if(*p == AV_PIX_FMT_D3D11 && ctx->hw_device_ctx && !ctx->hw_frames_ctx)
		{
			AVBufferRef *frames_ref = NULL;
			if(avcodec_get_hw_frames_parameters(ctx, ctx->hw_device_ctx, *p, &frames_ref) >= 0 && frames_ref)
			{
				AVHWFramesContext *frames = (AVHWFramesContext *)frames_ref->data;
				AVD3D11VAFramesContext *hwframes = (AVD3D11VAFramesContext *)frames->hwctx;
				hwframes->BindFlags |= D3D11_BIND_SHADER_RESOURCE;
				if(av_hwframe_ctx_init(frames_ref) < 0)
				{
					CHIAKI_LOGW(decoder->log, "Failed to initialize D3D11VA frames context, "
						"zero-copy decoding may be unavailable");
					av_buffer_unref(&frames_ref);
				}
				else
				{
					CHIAKI_LOGI(decoder->log, "D3D11VA frames context: %dx%d %s, pool=%d, bind=0x%x",
						frames->width, frames->height, av_get_pix_fmt_name(frames->sw_format),
						frames->initial_pool_size, hwframes->BindFlags);
					ctx->hw_frames_ctx = frames_ref;
				}
			}
			else
			{
				CHIAKI_LOGW(decoder->log, "Failed to derive D3D11VA frames parameters for %dx%d",
					ctx->width, ctx->height);
			}
		}

		return *p;
	}
	return AV_PIX_FMT_NONE;
}
#endif

CHIAKI_EXPORT ChiakiErrorCode chiaki_ffmpeg_decoder_init(ChiakiFfmpegDecoder *decoder, ChiakiLog *log,
		ChiakiCodec codec, unsigned int max_fps, const char *hw_decoder_name, AVBufferRef *hw_device_ctx,
		ChiakiFfmpegFrameAvailable frame_available_cb, void *frame_available_cb_user)
{
	ChiakiErrorCode err = chiaki_mutex_init(&decoder->mutex, false);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;
	chiaki_mutex_lock(&decoder->mutex);
	decoder->log = log;
	decoder->frame_available_cb = frame_available_cb;
	decoder->frame_available_cb_user = frame_available_cb_user;
	decoder->hdr_enabled = codec == CHIAKI_CODEC_H265_HDR;
	decoder->frames_lost = 0;
	decoder->frame_recovered = false;
	decoder->synthetic_packet_pts = 0;
	decoder->synthetic_framerate = (AVRational){max_fps > 0 ? (int)max_fps : 60, 1};
	decoder->synthetic_time_base = (AVRational){1, 1000000};
	decoder->synthetic_frame_duration_us = chiaki_ffmpeg_decoder_default_frame_duration_us(max_fps);
	decoder->synthetic_candidate_duration_us = decoder->synthetic_frame_duration_us;
	decoder->synthetic_last_sample_time_us = 0;
	decoder->synthetic_candidate_count = 0;
	decoder->synthetic_fastrecover_count = 0;

	decoder->hw_device_ctx = hw_device_ctx ? av_buffer_ref(hw_device_ctx) : NULL;
	decoder->hw_pix_fmt = AV_PIX_FMT_NONE;

#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(58, 10, 100)
	avcodec_register_all();
#endif
	enum AVCodecID av_codec = chiaki_codec_av_codec_id(codec);
	decoder->av_codec = avcodec_find_decoder(av_codec);
	if(!decoder->av_codec)
	{
		CHIAKI_LOGE(log, "%s Codec not available", chiaki_codec_name(codec));
		goto error_mutex;
	}

	decoder->codec_context = avcodec_alloc_context3(decoder->av_codec);
	if(!decoder->codec_context)
	{
		CHIAKI_LOGE(log, "Failed to alloc codec context");
		goto error_mutex;
	}

	if(hw_decoder_name)
	{
		CHIAKI_LOGI(log, "Trying to use hardware decoder \"%s\"", hw_decoder_name);
		enum AVHWDeviceType type = av_hwdevice_find_type_by_name(hw_decoder_name);
		if(type == AV_HWDEVICE_TYPE_NONE)
		{
			CHIAKI_LOGE(log, "Hardware decoder \"%s\" not found", hw_decoder_name);
			goto error_codec_context;
		}

		for(int i = 0;; i++)
		{
			const AVCodecHWConfig *config = avcodec_get_hw_config(decoder->av_codec, i);
			if(!config)
			{
				CHIAKI_LOGE(log, "avcodec_get_hw_config failed");
				goto error_codec_context;
			}
			if(config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX && config->device_type == type)
			{
				decoder->hw_pix_fmt = config->pix_fmt;
				break;
			}
		}

		if(!decoder->hw_device_ctx && av_hwdevice_ctx_create(&decoder->hw_device_ctx, type, NULL, NULL, 0) < 0)
		{
			CHIAKI_LOGE(log, "Failed to create hwdevice context");
			goto error_codec_context;
		}
		decoder->codec_context->hw_device_ctx = av_buffer_ref(decoder->hw_device_ctx);
		CHIAKI_LOGI(log, "Using hardware decoder \"%s\" with pix_fmt=%s", hw_decoder_name, av_get_pix_fmt_name(decoder->hw_pix_fmt));
	}

	decoder->codec_context->framerate = decoder->synthetic_framerate;
	decoder->codec_context->pkt_timebase = decoder->synthetic_time_base;
	decoder->codec_context->time_base = decoder->synthetic_time_base;

#if defined(_WIN32)
	// Negotiate the hardware pixel format ourselves so the decoder's texture
	// pool carries D3D11_BIND_SHADER_RESOURCE; see chiaki_ffmpeg_get_hw_format.
	if(decoder->hw_pix_fmt == AV_PIX_FMT_D3D11)
	{
		decoder->codec_context->opaque = decoder;
		decoder->codec_context->get_format = chiaki_ffmpeg_get_hw_format;
	}
#endif

	if(avcodec_open2(decoder->codec_context, decoder->av_codec, NULL) < 0)
	{
		CHIAKI_LOGE(log, "Failed to open codec context");
		goto error_codec_context;
	}
	chiaki_mutex_unlock(&decoder->mutex);
	return CHIAKI_ERR_SUCCESS;
error_codec_context:
	if(decoder->hw_device_ctx)
		av_buffer_unref(&decoder->hw_device_ctx);
	avcodec_free_context(&decoder->codec_context);
error_mutex:
	chiaki_mutex_unlock(&decoder->mutex);
	chiaki_mutex_fini(&decoder->mutex);
	return CHIAKI_ERR_UNKNOWN;
}

CHIAKI_EXPORT void chiaki_ffmpeg_decoder_fini(ChiakiFfmpegDecoder *decoder)
{
	chiaki_mutex_lock(&decoder->mutex);
	avcodec_free_context(&decoder->codec_context);
	if(decoder->hw_device_ctx)
		av_buffer_unref(&decoder->hw_device_ctx);
	chiaki_mutex_unlock(&decoder->mutex);
	chiaki_mutex_fini(&decoder->mutex);
}

CHIAKI_EXPORT bool chiaki_ffmpeg_decoder_video_sample_cb(uint8_t *buf, size_t buf_size, int32_t frames_lost, bool frame_recovered, void *user)
{
	ChiakiFfmpegDecoder *decoder = user;

	chiaki_mutex_lock(&decoder->mutex);
	decoder->last_sample_receive_us = chiaki_time_now_monotonic_us();
	decoder->frames_lost += frames_lost;
	decoder->frame_recovered = frame_recovered;
	if(decoder->synthetic_last_sample_time_us)
	{
		double observed_duration_us = (double)(chiaki_time_now_monotonic_us() - decoder->synthetic_last_sample_time_us);
		int64_t delivered_frames = (int64_t)frames_lost + 1;
		double default_duration_us = chiaki_ffmpeg_decoder_default_frame_duration_us((unsigned int)decoder->synthetic_framerate.num);
		if(delivered_frames > 1)
			observed_duration_us /= (double)delivered_frames;
		// Pre-clamp value: the fast-recovery gate below must judge against the
		// *real* arrival interval, not the clamped one. Bursts (two frames
		// landing together) clamp up to exactly default_duration_us and would
		// otherwise masquerade as healthy full-rate arrivals.
		const double observed_raw_us = observed_duration_us;
		if(observed_duration_us < default_duration_us)
			observed_duration_us = default_duration_us;
		else if(observed_duration_us > 1000000.0 / 15.0)
			observed_duration_us = 1000000.0 / 15.0;

		// Eager return to the known profile rate: a connect-time stall can lock
		// synthetic_frame_duration_us above the real rate (e.g. a ~48fps estimate
		// for a 60fps stream) while the symmetric 20%/3-sample gate below sits
		// just under its switch threshold forever (~19.9% for 48->60, plus jitter
		// resetting the candidate counter), latching the whole present pipeline
		// at the slow clock until the stream is restarted. The profile rate is
		// ground truth, not an estimate: three consecutive arrivals near it snap
		// straight back. Anything else falls through to the cautious adaptation
		// below, so genuinely slower content still adapts down normally.
		//
		// Judged on observed_raw_us (pre-clamp): a burst clamps to exactly
		// default_duration_us, so judging on the clamped value lets bursts pass
		// as healthy arrivals and snap a genuinely slower stream onto the fast
		// clock - which the 20% gate below then pushes straight back, an
		// oscillation that surfaces as micro-jitter. Three consecutive samples
		// (not two) keeps a burst pair from tripping it, and a burst is normally
		// followed by a late frame that resets the counter anyway.
		if(decoder->synthetic_frame_duration_us > default_duration_us
			&& fabs(observed_raw_us - default_duration_us) / default_duration_us <= 0.05)
		{
			if(++decoder->synthetic_fastrecover_count >= 3)
			{
				decoder->synthetic_frame_duration_us = default_duration_us;
				decoder->synthetic_candidate_duration_us = default_duration_us;
				decoder->synthetic_candidate_count = 0;
				decoder->synthetic_fastrecover_count = 0;
			}
		}
		else
			decoder->synthetic_fastrecover_count = 0;

		double candidate_diff = decoder->synthetic_candidate_duration_us > 0.0
			? fabs(observed_duration_us - decoder->synthetic_candidate_duration_us) / decoder->synthetic_candidate_duration_us
			: 1.0;
		double current_diff = decoder->synthetic_frame_duration_us > 0.0
			? fabs(observed_duration_us - decoder->synthetic_frame_duration_us) / decoder->synthetic_frame_duration_us
			: 1.0;
		if(current_diff >= 0.20)
		{
			if(candidate_diff <= 0.10)
				decoder->synthetic_candidate_count++;
			else
			{
				decoder->synthetic_candidate_duration_us = observed_duration_us;
				decoder->synthetic_candidate_count = 1;
			}
			if(decoder->synthetic_candidate_count >= 3)
			{
				decoder->synthetic_frame_duration_us = decoder->synthetic_candidate_duration_us;
				decoder->synthetic_candidate_count = 0;
			}
		}
		else
		{
			decoder->synthetic_candidate_duration_us = decoder->synthetic_frame_duration_us;
			decoder->synthetic_candidate_count = 0;
		}
	}
	decoder->synthetic_last_sample_time_us = chiaki_time_now_monotonic_us();

	int64_t synthetic_duration_pts = (int64_t)(decoder->synthetic_frame_duration_us + 0.5);
	if(synthetic_duration_pts < 1)
		synthetic_duration_pts = 1;
	if(frames_lost > 0)
		decoder->synthetic_packet_pts += synthetic_duration_pts * (int64_t)frames_lost;

	AVPacket *packet = av_packet_alloc();
	packet->data = buf;
	packet->size = buf_size;
	packet->pts = decoder->synthetic_packet_pts;
	packet->dts = decoder->synthetic_packet_pts;
	packet->duration = synthetic_duration_pts;
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(59, 8, 100)
	packet->time_base = decoder->synthetic_time_base;
#endif
	decoder->synthetic_packet_pts += synthetic_duration_pts;
	int r;
send_packet:
	r = avcodec_send_packet(decoder->codec_context, packet);
	if(r != 0)
	{
		if(r == AVERROR(EAGAIN))
		{
			CHIAKI_LOGW(decoder->log, "AVCodec internal buffer is full, dropping a decoded frame to push new packet");
			AVFrame *frame = av_frame_alloc();
			if(!frame)
			{
				CHIAKI_LOGE(decoder->log, "Failed to alloc AVFrame");
				goto hell;
			}
			r = avcodec_receive_frame(decoder->codec_context, frame);
			av_frame_free(&frame);
			if(r != 0)
			{
				CHIAKI_LOGE(decoder->log, "Failed to pull frame from full codec buffer");
				goto hell;
			}
			decoder->frames_lost++;
			goto send_packet;
		}
		else
		{
			char errbuf[128];
			av_make_error_string(errbuf, sizeof(errbuf), r);
			CHIAKI_LOGE(decoder->log, "Failed to push frame: %s", errbuf);
			goto hell;
		}
	}
	av_packet_free(&packet);
	chiaki_mutex_unlock(&decoder->mutex);
	decoder->frame_available_cb(decoder, decoder->frame_available_cb_user);
	return true;
hell:
	av_packet_free(&packet);
	chiaki_mutex_unlock(&decoder->mutex);
	return false;
}

CHIAKI_EXPORT ChiakiFfmpegFrame chiaki_ffmpeg_decoder_pull_frame(ChiakiFfmpegDecoder *decoder, int32_t *frames_lost)
{
	chiaki_mutex_lock(&decoder->mutex);
	double synthetic_duration = decoder->synthetic_frame_duration_us / 1000000.0;
	// always try to pull as much as possible and return only the very last frame
	AVFrame *frame_last = NULL;
	AVFrame *frame = NULL;
	while(true)
	{
		AVFrame *next_frame;
		if(frame_last)
		{
			av_frame_unref(frame_last);
			next_frame = frame_last;
		}
		else
		{
			next_frame = av_frame_alloc();
			if(!next_frame)
				break;
		}
		frame_last = frame;
		frame = next_frame;
		int r = avcodec_receive_frame(decoder->codec_context, frame);
		if(r)
		{
			if(r != AVERROR(EAGAIN))
				CHIAKI_LOGE(decoder->log, "Decoding with FFMPEG failed");
			av_frame_free(&frame);
			frame = frame_last;
			break;
		}
	}
	*frames_lost = decoder->frames_lost;
	bool recovered = false;
	if(frame && decoder->frame_recovered)
	{
		recovered = true;
		decoder->frame_recovered = false;
		frame->decode_error_flags |= 1;
	}
	decoder->frames_lost = 0;
	int64_t last_sample_receive_us = decoder->last_sample_receive_us;
	AVRational pkt_timebase = decoder->codec_context->pkt_timebase;
	AVRational ctx_timebase = decoder->codec_context->time_base;
	AVRational framerate = decoder->codec_context->framerate;
	chiaki_mutex_unlock(&decoder->mutex);

	ChiakiFfmpegFrame frame_plus_stats = {};
	frame_plus_stats.frame = frame;
	frame_plus_stats.recovered = recovered;
	frame_plus_stats.receive_us = last_sample_receive_us;
	if(frame)
	{
		chiaki_ffmpeg_frame_get_timing(
			frame,
			pkt_timebase,
			ctx_timebase,
			framerate,
			&frame_plus_stats.pts,
			&frame_plus_stats.duration);
		if(frame->duration <= 0)
			frame_plus_stats.duration = synthetic_duration;
	}

	return frame_plus_stats;
}

CHIAKI_EXPORT void chiaki_ffmpeg_frame_get_timing(
	AVFrame *frame,
	AVRational pkt_timebase,
	AVRational ctx_timebase,
	AVRational framerate,
	double *out_pts,
	double *out_duration)
{
	AVRational time_base = pkt_timebase;
	if(time_base.num <= 0 || time_base.den <= 0)
		time_base = ctx_timebase;
	if(time_base.num <= 0 || time_base.den <= 0)
		time_base = (AVRational){1, 1000000};

	int64_t best_effort_pts = frame->best_effort_timestamp;
	int64_t raw_pts = frame->pts;
	int64_t pts = best_effort_pts;
	if(pts == AV_NOPTS_VALUE)
		pts = raw_pts;
	if(pts == AV_NOPTS_VALUE)
		pts = 0;
	*out_pts = av_q2d(time_base) * (double)pts;

	if(frame->duration > 0)
	{
		*out_duration = av_q2d(time_base) * (double)frame->duration;
		return;
	}

	double fps = (framerate.num > 0 && framerate.den > 0) ? av_q2d(framerate) : 60.0;
	if(fps <= 0.0)
		fps = 60.0;
	*out_duration = 1.0 / fps;
}

CHIAKI_EXPORT enum AVPixelFormat chiaki_ffmpeg_decoder_get_pixel_format(ChiakiFfmpegDecoder *decoder)
{
	if (decoder->hw_device_ctx) {
		return decoder->hdr_enabled
			? AV_PIX_FMT_P010LE
			: AV_PIX_FMT_NV12;
	} else {
		return decoder->hdr_enabled
			? AV_PIX_FMT_YUV420P10LE
			: AV_PIX_FMT_YUV420P;
	}
}
