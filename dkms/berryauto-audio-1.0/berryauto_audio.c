// SPDX-License-Identifier: GPL-2.0
/*
 * BerryAuto Custom Virtual ALSA Driver
 * Provides a strict, zero-starvation hrtimer-driven loopback
 * for Android Auto media and microphone streaming.
 */

#include <linux/hrtimer.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/pcm.h>

MODULE_AUTHOR("BerryAuto Project");
MODULE_DESCRIPTION("BerryAuto Virtual Audio Hardware");
MODULE_LICENSE("GPL");

/* Hardcoded Constraints for Android Auto */
static const struct snd_pcm_hardware berryauto_media_hw = {
    .info = SNDRV_PCM_INFO_INTERLEAVED | SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_MMAP_VALID,
    .formats = SNDRV_PCM_FMTBIT_S16_LE,
    .rates = SNDRV_PCM_RATE_48000,
    .rate_min = 48000,
    .rate_max = 48000,
    .channels_min = 2,
    .channels_max = 2,
    .buffer_bytes_max = 48000 * 4 * 2, // 2 seconds
    .period_bytes_min = 480 * 4,       // 10 ms
    .period_bytes_max = 4800 * 4,      // 100 ms
    .periods_min = 2,
    .periods_max = 16,
};

static const struct snd_pcm_hardware berryauto_mic_hw = {
    .info = SNDRV_PCM_INFO_INTERLEAVED | SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_MMAP_VALID,
    .formats = SNDRV_PCM_FMTBIT_S16_LE,
    .rates = SNDRV_PCM_RATE_16000,
    .rate_min = 16000,
    .rate_max = 16000,
    .channels_min = 1,
    .channels_max = 1,
    .buffer_bytes_max = 16000 * 2 * 2, // 2 seconds
    .period_bytes_min = 160 * 2,       // 10 ms
    .period_bytes_max = 1600 * 2,      // 100 ms
    .periods_min = 2,
    .periods_max = 16,
};

struct berryauto_cable
{
    spinlock_t lock;
    struct hrtimer timer;
    ktime_t last_time;
    u64 frac_frames;

    unsigned int rate;
    unsigned int frame_bytes;

    bool play_running;
    bool capt_running;

    struct snd_pcm_substream* play_substream;
    struct snd_pcm_substream* capt_substream;

    char* play_buf;
    char* capt_buf;

    unsigned int play_buf_size; // in frames
    unsigned int capt_buf_size; // in frames
    unsigned int play_period_size;
    unsigned int capt_period_size;

    unsigned int play_pos; // in frames
    unsigned int capt_pos; // in frames
    unsigned int play_period_pos;
    unsigned int capt_period_pos;
};

struct berryauto_card
{
    struct snd_card* card;
    struct snd_pcm* pcm[2];
    struct berryauto_cable cables[2]; // 0 = Media, 1 = Mic
};

static struct platform_device* berryauto_device;

/* High-resolution timer simulating actual soundcard clock */
static enum hrtimer_restart berryauto_hrtimer_callback(struct hrtimer* timer)
{
    struct berryauto_cable* cable = container_of(timer, struct berryauto_cable, timer);
    unsigned long flags;
    bool play_period = false;
    bool capt_period = false;
    u64 now, delta_ns;
    unsigned int frames_to_process = 0;

    spin_lock_irqsave(&cable->lock, flags);

    if (!cable->play_running && !cable->capt_running)
    {
        spin_unlock_irqrestore(&cable->lock, flags);
        return HRTIMER_NORESTART;
    }

    now = ktime_get_ns();
    delta_ns = now - cable->last_time;
    cable->last_time = now;

    /* Calculate frames generated since last tick (ns -> frames) */
    cable->frac_frames += ((u64)cable->rate * delta_ns);
    frames_to_process = cable->frac_frames / 1000000000ULL;
    cable->frac_frames %= 1000000000ULL;

    if (frames_to_process > 0)
    {
        unsigned int src_pos = cable->play_pos;
        unsigned int dst_pos = cable->capt_pos;
        unsigned int frames = frames_to_process;

        while (frames > 0)
        {
            unsigned int chunk = frames;

            /* Constrain chunk to the end of the circular buffer */
            if (cable->play_running && src_pos + chunk > cable->play_buf_size)
                chunk = cable->play_buf_size - src_pos;
            if (cable->capt_running && dst_pos + chunk > cable->capt_buf_size)
                chunk = cable->capt_buf_size - dst_pos;

            unsigned int bytes = chunk * cable->frame_bytes;

            /* If OS is playing and Daemon is reading: copy memory */
            if (cable->play_running && cable->capt_running)
            {
                memcpy(cable->capt_buf + (dst_pos * cable->frame_bytes),
                       cable->play_buf + (src_pos * cable->frame_bytes), bytes);
            }
            /* If OS stopped playing, but Daemon is reading: copy silence */
            else if (cable->capt_running)
            {
                memset(cable->capt_buf + (dst_pos * cable->frame_bytes), 0, bytes);
            }

            if (cable->play_running)
            {
                src_pos += chunk;
                if (src_pos >= cable->play_buf_size)
                    src_pos = 0;
                cable->play_period_pos += chunk;
                if (cable->play_period_pos >= cable->play_period_size)
                {
                    cable->play_period_pos %= cable->play_period_size;
                    play_period = true;
                }
            }

            if (cable->capt_running)
            {
                dst_pos += chunk;
                if (dst_pos >= cable->capt_buf_size)
                    dst_pos = 0;
                cable->capt_period_pos += chunk;
                if (cable->capt_period_pos >= cable->capt_period_size)
                {
                    cable->capt_period_pos %= cable->capt_period_size;
                    capt_period = true;
                }
            }
            frames -= chunk;
        }
        cable->play_pos = src_pos;
        cable->capt_pos = dst_pos;
    }

    /* 1 millisecond tick rate */
    hrtimer_forward_now(&cable->timer, ktime_set(0, 1000000));

    spin_unlock_irqrestore(&cable->lock, flags);

    /* Notify ALSA midlevel outside the spinlock */
    if (play_period)
        snd_pcm_period_elapsed(cable->play_substream);
    if (capt_period)
        snd_pcm_period_elapsed(cable->capt_substream);

    return HRTIMER_RESTART;
}

/* Helper to route Dev0/Dev1 to the correct Media/Mic cables */
static struct berryauto_cable* get_cable(struct snd_pcm_substream* substream, struct berryauto_card* bcard)
{
    int dev = substream->pcm->device;
    int dir = substream->stream;

    if (dev == 0 && dir == SNDRV_PCM_STREAM_PLAYBACK)
        return &bcard->cables[0]; // OS Media Out
    if (dev == 1 && dir == SNDRV_PCM_STREAM_CAPTURE)
        return &bcard->cables[0]; // Daemon Media In
    if (dev == 1 && dir == SNDRV_PCM_STREAM_PLAYBACK)
        return &bcard->cables[1]; // Daemon Mic Out
    if (dev == 0 && dir == SNDRV_PCM_STREAM_CAPTURE)
        return &bcard->cables[1]; // OS Mic In

    return NULL;
}

static int berryauto_pcm_open(struct snd_pcm_substream* substream)
{
    struct berryauto_card* bcard = substream->private_data;
    struct berryauto_cable* cable = get_cable(substream, bcard);
    struct snd_pcm_runtime* runtime = substream->runtime;

    if (!cable)
        return -EINVAL;

    if (cable == &bcard->cables[0])
    {
        runtime->hw = berryauto_media_hw;
        cable->rate = 48000;
        cable->frame_bytes = 4;
    }
    else
    {
        runtime->hw = berryauto_mic_hw;
        cable->rate = 16000;
        cable->frame_bytes = 2;
    }

    return 0;
}

static int berryauto_pcm_close(struct snd_pcm_substream* substream)
{
    return 0;
}

static int berryauto_pcm_prepare(struct snd_pcm_substream* substream)
{
    struct berryauto_card* bcard = substream->private_data;
    struct berryauto_cable* cable = get_cable(substream, bcard);
    struct snd_pcm_runtime* runtime = substream->runtime;

    spin_lock_irq(&cable->lock);

    if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
    {
        cable->play_substream = substream;
        cable->play_buf = runtime->dma_area;
        cable->play_buf_size = runtime->buffer_size;
        cable->play_period_size = runtime->period_size;
        cable->play_pos = 0;
        cable->play_period_pos = 0;
    }
    else
    {
        cable->capt_substream = substream;
        cable->capt_buf = runtime->dma_area;
        cable->capt_buf_size = runtime->buffer_size;
        cable->capt_period_size = runtime->period_size;
        cable->capt_pos = 0;
        cable->capt_period_pos = 0;
    }

    spin_unlock_irq(&cable->lock);
    return 0;
}

static int berryauto_pcm_trigger(struct snd_pcm_substream* substream, int cmd)
{
    struct berryauto_card* bcard = substream->private_data;
    struct berryauto_cable* cable = get_cable(substream, bcard);
    bool is_play = (substream->stream == SNDRV_PCM_STREAM_PLAYBACK);

    spin_lock_irq(&cable->lock);

    if (cmd == SNDRV_PCM_TRIGGER_START || cmd == SNDRV_PCM_TRIGGER_RESUME)
    {
        if (is_play)
            cable->play_running = true;
        else
            cable->capt_running = true;

        if (!hrtimer_active(&cable->timer))
        {
            cable->last_time = ktime_get_ns();
            cable->frac_frames = 0;
            hrtimer_start(&cable->timer, ktime_set(0, 1000000), HRTIMER_MODE_REL_SOFT);
        }
    }
    else if (cmd == SNDRV_PCM_TRIGGER_STOP || cmd == SNDRV_PCM_TRIGGER_SUSPEND)
    {
        if (is_play)
            cable->play_running = false;
        else
            cable->capt_running = false;

        /* The hrtimer will cleanly exit on its next tick if both are false */
    }

    spin_unlock_irq(&cable->lock);
    return 0;
}

static snd_pcm_uframes_t berryauto_pcm_pointer(struct snd_pcm_substream* substream)
{
    struct berryauto_card* bcard = substream->private_data;
    struct berryauto_cable* cable = get_cable(substream, bcard);
    snd_pcm_uframes_t pos;

    spin_lock_irq(&cable->lock);
    if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
        pos = cable->play_pos;
    else
        pos = cable->capt_pos;
    spin_unlock_irq(&cable->lock);

    return pos;
}

static const struct snd_pcm_ops berryauto_pcm_ops = {
    .open = berryauto_pcm_open,
    .close = berryauto_pcm_close,
    .prepare = berryauto_pcm_prepare,
    .trigger = berryauto_pcm_trigger,
    .pointer = berryauto_pcm_pointer,
};

static int berryauto_probe(struct platform_device* devptr)
{
    struct snd_card* card;
    struct berryauto_card* bcard;
    int err, i;

    err = snd_card_new(&devptr->dev, -1, "BerryAutoAudio", THIS_MODULE, sizeof(struct berryauto_card), &card);
    if (err < 0)
        return err;

    bcard = card->private_data;
    bcard->card = card;

    for (i = 0; i < 2; i++)
    {
        spin_lock_init(&bcard->cables[i].lock);
        hrtimer_init(&bcard->cables[i].timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL_SOFT);
        bcard->cables[i].timer.function = berryauto_hrtimer_callback;
    }

    /* Device 0: OS facing */
    err = snd_pcm_new(card, "BerryAuto OS Node", 0, 1, 1, &bcard->pcm[0]);
    if (err < 0)
        goto error;
    snd_pcm_set_ops(bcard->pcm[0], SNDRV_PCM_STREAM_PLAYBACK, &berryauto_pcm_ops);
    snd_pcm_set_ops(bcard->pcm[0], SNDRV_PCM_STREAM_CAPTURE, &berryauto_pcm_ops);
    bcard->pcm[0]->private_data = bcard;
    snd_pcm_set_managed_buffer_all(bcard->pcm[0], SNDRV_DMA_TYPE_VMALLOC, NULL, 0, 0);

    /* Device 1: Daemon facing */
    err = snd_pcm_new(card, "BerryAuto Daemon Node", 1, 1, 1, &bcard->pcm[1]);
    if (err < 0)
        goto error;
    snd_pcm_set_ops(bcard->pcm[1], SNDRV_PCM_STREAM_PLAYBACK, &berryauto_pcm_ops);
    snd_pcm_set_ops(bcard->pcm[1], SNDRV_PCM_STREAM_CAPTURE, &berryauto_pcm_ops);
    bcard->pcm[1]->private_data = bcard;
    snd_pcm_set_managed_buffer_all(bcard->pcm[1], SNDRV_DMA_TYPE_VMALLOC, NULL, 0, 0);

    strcpy(card->driver, "BerryAuto");
    strcpy(card->shortname, "BerryAutoAudio");
    strcpy(card->longname, "BerryAuto Virtual Hardware Audio");

    err = snd_card_register(card);
    if (err < 0)
        goto error;

    platform_set_drvdata(devptr, card);
    return 0;

error:
    snd_card_free(card);
    return err;
}

static int berryauto_remove(struct platform_device* devptr)
{
    struct snd_card* card = platform_get_drvdata(devptr);
    struct berryauto_card* bcard = card->private_data;

    hrtimer_cancel(&bcard->cables[0].timer);
    hrtimer_cancel(&bcard->cables[1].timer);

    snd_card_free(card);
    return 0;
}

static struct platform_driver berryauto_driver = {
    .probe = berryauto_probe,
    .remove = berryauto_remove,
    .driver =
        {
            .name = "snd_berryauto",
        },
};

static int __init berryauto_init(void)
{
    int err;
    err = platform_driver_register(&berryauto_driver);
    if (err < 0)
        return err;

    berryauto_device = platform_device_register_simple("snd_berryauto", 0, NULL, 0);
    if (IS_ERR(berryauto_device))
    {
        platform_driver_unregister(&berryauto_driver);
        return PTR_ERR(berryauto_device);
    }
    return 0;
}

static void __exit berryauto_exit(void)
{
    platform_device_unregister(berryauto_device);
    platform_driver_unregister(&berryauto_driver);
}

module_init(berryauto_init) module_exit(berryauto_exit)
