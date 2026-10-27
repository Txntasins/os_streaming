/*
 * Bare-Metal Audio Subsystem for Raspberry Pi 5 (BCM2712 + RP1) & QEMU AArch64
 *
 * Implements all three bare-metal audio architectures required by the OS Streaming project:
 *   1. PWM Audio Driver (RP1 PWM0/PWM1 @ 0x1f00098000, carrier 48 kHz, 11-bit duty cycle, FIFO mode)
 *   2. I2S DAC Driver + DMA Ring Buffer (RP1 DesignWare I2S0 @ 0x1f000a0000 -> PCM5102A 32-bit BCLK/LRCLK)
 *   3. HDMI Audio Driver (`vc4-hdmi` architecture @ 0x107c701400 on BCM2712 VideoCore VII):
 *      - EDID CEA-861 Extension Block parser (Short Audio Descriptors for LPCM 48 kHz)
 *      - HDMI Audio Clock Regeneration (ACR) N / CTS calculation & packet programming:
 *        128 * fs = f_tmds * (N / CTS) -> For 48 kHz @ 74.25 MHz (720p60): N = 6144, CTS = 74250
 *      - IEC60958 / IEC61937 Subframe Formatter (24-bit PCM + Channel Status + Parity + B/M/W preambles)
 *      - CEA-861-D Audio InfoFrame (Type 0x84, Version 0x01, Length 0x0A, Checksum) into HDMI Packet RAM
 *      - MAI (Multi-channel Audio Interconnect) FIFO + DMA pacing
 *
 * Safe Dual-Mode Execution:
 *   - Automatically detects whether running on QEMU `virt` (RAM @ 0x40000000) or real Raspberry Pi 5
 *     hardware (via `MIDR_EL1` Cortex-A76 part `0xd0b` + board mode flag).
 *   - On QEMU `virt`, MMIO writes go to a shadow register bank so students can inspect all hardware
 *     registers, EDID parsing, IEC60958 subframes, N/CTS values, and FIFO/DMA statistics via `os> audio`
 *     or `/api/audio` without triggering a synchronous Data Abort on unmapped Pi 5 physical addresses.
 *   - On real Raspberry Pi 5 (`pi5-av-bringup`), writes directly to physical MMIO addresses.
 */
#include "kernel.h"
#include <string.h>

/* ---------- Hardware Physical Base Addresses on Raspberry Pi 5 ---------- */
#define RP1_IO_BASE             0x1f00000000ULL
#define RP1_GPIO_BASE           0x1f000d0000ULL     /* RP1 IO_BANK0 GPIO control */
#define RP1_CLOCKS_BASE         0x1f00018000ULL     /* RP1 Clock manager */
#define RP1_PWM0_BASE           0x1f00098000ULL     /* RP1 PWM0 controller */
#define RP1_I2S0_BASE           0x1f000a0000ULL     /* RP1 DesignWare I2S0 controller */
#define RP1_DMA_BASE            0x1f00188000ULL     /* RP1 DesignWare AXI DMAC */

#define BCM2712_HDMI0_BASE      0x107c701400ULL     /* BCM2712 vc4-hdmi0 core registers */
#define BCM2712_HDMI0_RAM_BASE  0x107c702400ULL     /* BCM2712 HDMI0 Packet RAM (InfoFrames) */
#define BCM2712_HD_BASE         0x107c700000ULL     /* BCM2712 HDMI Top / MAI registers */
#define BCM2712_BSC_HDMI0       0x107c704800ULL     /* BCM2712 HDMI0 DDC / I2C controller for EDID */

/* ---------- 1. RP1 PWM0 Register Offsets ---------- */
#define PWM_GLOBAL_CTRL         0x00
#define PWM_FIFO_CTRL           0x04
#define PWM_COMMON_RANGE        0x08
#define PWM_COMMON_DUTY         0x0c
#define PWM_CHAN0_CTRL          0x14
#define PWM_CHAN0_RANGE         0x18
#define PWM_CHAN0_PHASE         0x1c
#define PWM_CHAN0_DUTY          0x20
#define PWM_CHAN1_CTRL          0x24
#define PWM_CHAN1_RANGE         0x28
#define PWM_CHAN1_PHASE         0x2c
#define PWM_CHAN1_DUTY          0x30
#define PWM_FIFO_IN             0x40

#define PWM_CTRL_CHAN0_EN       (1U << 0)
#define PWM_CTRL_CHAN1_EN       (1U << 1)
#define PWM_CTRL_SET_UPDATE     (1U << 31)
#define PWM_CHAN_MODE_FIFO      (0x2U << 0)         /* Trailing-edge Markov / FIFO pop mode */
#define PWM_CHAN_FIFO_POP_MASK  (1U << 8)
#define PWM_FIFO_CLEAR          (1U << 31)
#define PWM_FIFO_DREQ_EN        (1U << 30)

/* ---------- 2. RP1 DesignWare I2S0 Register Offsets ---------- */
#define I2S_IER                 0x000               /* Global Enable Register */
#define I2S_IRER                0x004               /* Receiver Block Enable */
#define I2S_ITER                0x008               /* Transmitter Block Enable */
#define I2S_CER                 0x00c               /* Clock Generation Enable */
#define I2S_CCR                 0x010               /* Clock Configuration Register */
#define I2S_TXFFR               0x018               /* Transmitter Block FIFO Reset */
#define I2S_LTHR0               0x020               /* Left Transmit Holding Register 0 */
#define I2S_RTHR0               0x024               /* Right Transmit Holding Register 0 */
#define I2S_TER0                0x02c               /* Transmit Enable Register 0 */
#define I2S_TCR0                0x034               /* Transmit Configuration Register 0 (Word length) */
#define I2S_ISR0                0x038               /* Interrupt Status Register 0 */
#define I2S_IMR0                0x03c               /* Interrupt Mask Register 0 */
#define I2S_TFCR0               0x04c               /* Transmit FIFO Configuration Register 0 */
#define I2S_TXDMA               0x1c8               /* Transmitter Block DMA Register */
#define I2S_DMACR               0x200               /* DMA Control Register */

/* ---------- 3. BCM2712 VideoCore VII vc4-hdmi Register Offsets ---------- */
#define VC4_HD_MAI_CTL          0x014               /* MAI Control (Enable, Channels, Parity, Flush) */
#define VC4_HD_MAI_THR          0x018               /* MAI DMA DREQ Thresholds */
#define VC4_HD_MAI_FMT          0x01c               /* MAI Sample Rate & Format */
#define VC4_HD_MAI_DATA         0x020               /* MAI Audio Subframe FIFO Data Port */
#define VC4_HD_MAI_SMP          0x02c               /* MAI Audio Clock Divider */

#define VC4_HDMI_AUDIO_PACKET_CONFIG 0x0b8          /* CEA Audio Sample Packet & B-frame mask */
#define VC4_HDMI_RAM_PACKET_CONFIG   0x0bc          /* InfoFrame RAM Packet Enable (Bit 4 = Audio 0x84) */
#define VC4_HDMI_RAM_PACKET_STATUS   0x0c4          /* InfoFrame RAM Packet Active Status */
#define VC4_HDMI_CRP_CFG             0x0c8          /* Clock Recovery Packet Config (External CTS + N) */
#define VC4_HDMI_CTS_0               0x0cc          /* Audio Clock Regeneration CTS Period 0 */
#define VC4_HDMI_CTS_1               0x0d0          /* Audio Clock Regeneration CTS Period 1 */
#define VC4_HDMI_MAI_CHANNEL_MAP     0x09c          /* HDMI MAI Speaker Channel Mapping */
#define VC4_HDMI_MAI_CONFIG          0x0a0          /* HDMI MAI Channel Mask & Bit Reversal */

/* MAI Control Bits (matches Linux `drivers/gpu/drm/vc4/vc4_hdmi_regs.h`) */
#define VC4_HD_MAI_CTL_RESET         (1U << 0)
#define VC4_HD_MAI_CTL_ERRORF        (1U << 1)
#define VC4_HD_MAI_CTL_UNDERF        (1U << 2)
#define VC4_HD_MAI_CTL_ENABLE       (1U << 3)
#define VC4_HD_MAI_CTL_CHNUM_SHIFT   4
#define VC4_HD_MAI_CTL_PAREN         (1U << 9)
#define VC4_HD_MAI_CTL_Wholsmp       (1U << 12)
#define VC4_HD_MAI_CTL_CHALIGN       (1U << 13)
#define VC4_HD_MAI_CTL_BUSY          (1U << 14)
#define VC4_HD_MAI_CTL_DLATE         (1U << 15)

/* ---------- Shadow Register Bank for QEMU Safe Inspection + Real Pi 5 MMIO ---------- */
static int g_hw_direct_mmio = 0;                    /* 1 when running on bare-metal Pi 5, 0 on QEMU virt */

struct audio_hw_state {
    int active_backend;                             /* AUDIO_BACKEND_PWM / I2S / HDMI */
    u32 sample_rate;                                /* 48000 Hz standard */
    u32 channels;                                   /* 2 (Stereo) */
    u32 bits_per_sample;                            /* 16-bit (PWM) or 24/32-bit (I2S & HDMI) */

    /* PWM state */
    u32 pwm_clock_hz;                               /* 50,000,000 Hz RP1 audio clock */
    u32 pwm_range;                                  /* 1042 ticks -> ~47,984 Hz PWM carrier */
    u32 pwm_ctrl_reg;
    u32 pwm_fifo_reg;
    u32 pwm_last_duty_l;
    u32 pwm_last_duty_r;
    u64 pwm_samples_pushed;

    /* I2S + DMA state */
    u32 i2s_bclk_hz;                                /* 48000 * 64 = 3,072,000 Hz */
    u32 i2s_ccr_reg;
    u32 i2s_tcr0_reg;
    u32 i2s_dmacr_reg;
    u32 dma_ring_frames;
    u32 dma_write_idx;
    u32 dma_read_idx;
    u64 i2s_samples_pushed;

    /* HDMI vc4-hdmi state */
    u32 hdmi_tmds_clock_hz;                         /* 74,250,000 Hz (720p60) or 148,500,000 Hz (1080p60) */
    u32 hdmi_acr_n;                                 /* N = 6144 for 48 kHz */
    u32 hdmi_acr_cts;                               /* CTS = 74250 for 720p60 @ 48 kHz */
    u32 hdmi_mai_ctl_reg;
    u32 hdmi_mai_fmt_reg;
    u32 hdmi_mai_thr_reg;
    u32 hdmi_crp_cfg_reg;
    u32 hdmi_ram_pkt_cfg_reg;
    u8  hdmi_infoframe[14];                         /* 3-byte Header (0x84, 0x01, 0x0A) + Checksum + 10-byte Payload */
    u8  hdmi_edid_sad[3];                           /* Parsed CEA-861 Short Audio Descriptor (LPCM, 2ch, 32/44.1/48kHz) */
    int hdmi_edid_valid;
    u8  iec60958_cs[24];                            /* 192-bit IEC60958 Channel Status block */
    u32 iec60958_frame_idx;                         /* 0..191 block frame counter (0 = B preamble) */
    u32 last_iec_subframe_l;
    u32 last_iec_subframe_r;
    u64 hdmi_subframes_pushed;

    /* Shared Audio Master Clock for A/V Sync */
    u64 total_frames_rendered;
    u64 start_uptime_ms;
};

static struct audio_hw_state g_aud;

/* 16 KB DMA-aligned ring buffer for I2S / HDMI PCM frames */
#define AUDIO_DMA_RING_FRAMES 2048
static int32_t __attribute__((aligned(64))) g_dma_ring_buf[AUDIO_DMA_RING_FRAMES * 2];

static inline void aud_mmio_wr32(u64 phys_addr, u32 val) {
    if (g_hw_direct_mmio) {
        *(volatile u32 *)phys_addr = val;
        dmb();
    } else {
        (void)phys_addr;
        (void)val;
    }
}

/* ---------- Integer Sine Lut (64-entry Quarter Wave, 16-bit Q15) for 440Hz / 1kHz Test Tone ---------- */
static const int16_t sin_q15_table[64] = {
        0,   804,  1608,  2410,  3212,  4011,  4808,  5602,
     6393,  7179,  7962,  8739,  9512, 10278, 11039, 11793,
    12539, 13279, 14010, 14732, 15446, 16151, 16846, 17530,
    18204, 18868, 19519, 20159, 20787, 21403, 22005, 22594,
    23170, 23731, 24279, 24811, 25329, 25832, 26319, 26790,
    27245, 27683, 28105, 28510, 28898, 29268, 29621, 29956,
    30273, 30571, 30852, 31113, 31356, 31580, 31785, 31971,
    32137, 32285, 32412, 32521, 32609, 32678, 32728, 32757
};

static int16_t fast_sin_pcm16(u32 phase_256) {
    u32 idx = phase_256 & 0xffU;
    u32 q = idx >> 6;
    u32 off = idx & 0x3fU;
    int16_t v = (q & 1) ? sin_q15_table[63 - off] : sin_q15_table[off];
    return (q & 2) ? (int16_t)(-v) : v;
}

/* ============================================================================
 * 1. PWM Audio Driver (RP1 PWM0 Channels 0 & 1 on GPIO 12/13 or 18/19)
 * ============================================================================ */
static void pwm_audio_init(u32 sample_rate) {
    g_aud.pwm_clock_hz = 50000000U;                         /* 50 MHz RP1 PLL_AUDIO / XOSC */
    g_aud.pwm_range    = g_aud.pwm_clock_hz / sample_rate;  /* 50MHz / 48kHz = 1041 ticks (~10-bit resolution) */

    /* Configure RP1 PWM0 Channel 0 (Left) & Channel 1 (Right) in FIFO pop mode */
    g_aud.pwm_ctrl_reg = PWM_CTRL_CHAN0_EN | PWM_CTRL_CHAN1_EN | PWM_CTRL_SET_UPDATE;
    g_aud.pwm_fifo_reg = PWM_FIFO_CLEAR | PWM_FIFO_DREQ_EN;

    aud_mmio_wr32(RP1_PWM0_BASE + PWM_FIFO_CTRL, g_aud.pwm_fifo_reg);
    aud_mmio_wr32(RP1_PWM0_BASE + PWM_CHAN0_RANGE, g_aud.pwm_range);
    aud_mmio_wr32(RP1_PWM0_BASE + PWM_CHAN1_RANGE, g_aud.pwm_range);
    aud_mmio_wr32(RP1_PWM0_BASE + PWM_CHAN0_CTRL, PWM_CHAN_MODE_FIFO | PWM_CHAN_FIFO_POP_MASK);
    aud_mmio_wr32(RP1_PWM0_BASE + PWM_CHAN1_CTRL, PWM_CHAN_MODE_FIFO | PWM_CHAN_FIFO_POP_MASK);
    aud_mmio_wr32(RP1_PWM0_BASE + PWM_GLOBAL_CTRL, g_aud.pwm_ctrl_reg);
}

/* Convert signed 16-bit PCM [-32768..32767] to unsigned PWM duty cycle [0..pwm_range] */
static void pwm_audio_write_frame(int16_t left, int16_t right) {
    u32 ul = (u32)((int32_t)left + 32768);
    u32 ur = (u32)((int32_t)right + 32768);
    u32 duty_l = (ul * g_aud.pwm_range) >> 16;
    u32 duty_r = (ur * g_aud.pwm_range) >> 16;

    g_aud.pwm_last_duty_l = duty_l;
    g_aud.pwm_last_duty_r = duty_r;
    g_aud.pwm_samples_pushed += 2;

    aud_mmio_wr32(RP1_PWM0_BASE + PWM_FIFO_IN, duty_l);
    aud_mmio_wr32(RP1_PWM0_BASE + PWM_FIFO_IN, duty_r);
}

/* ============================================================================
 * 2. I2S DAC Driver + DMA Ring Buffer (RP1 DesignWare I2S0 -> PCM5102A DAC)
 * ============================================================================ */
static void i2s_audio_init(u32 sample_rate) {
    g_aud.i2s_bclk_hz   = sample_rate * 32U * 2U;           /* 64 BCLKs per stereo frame = 3.072 MHz */
    g_aud.i2s_ccr_reg   = (0x2U << 3) | 0x0U;               /* WSS = 32 SCLK cycles, NO gated SCLK */
    g_aud.i2s_tcr0_reg  = 0x4U;                             /* WLEN = 24-bit / 32-bit word resolution */
    g_aud.i2s_dmacr_reg = (1U << 17);                       /* TX Block DMA Enable */
    g_aud.dma_ring_frames = AUDIO_DMA_RING_FRAMES;
    g_aud.dma_write_idx = 0;
    g_aud.dma_read_idx  = 0;

    aud_mmio_wr32(RP1_I2S0_BASE + I2S_IER, 0);
    aud_mmio_wr32(RP1_I2S0_BASE + I2S_ITER, 0);
    aud_mmio_wr32(RP1_I2S0_BASE + I2S_TXFFR, 1);            /* Flush TX FIFO */
    aud_mmio_wr32(RP1_I2S0_BASE + I2S_CCR, g_aud.i2s_ccr_reg);
    aud_mmio_wr32(RP1_I2S0_BASE + I2S_TCR0, g_aud.i2s_tcr0_reg);
    aud_mmio_wr32(RP1_I2S0_BASE + I2S_TFCR0, 8);            /* FIFO empty threshold = 8 words */
    aud_mmio_wr32(RP1_I2S0_BASE + I2S_TER0, 1);             /* Enable TX Channel 0 */
    aud_mmio_wr32(RP1_I2S0_BASE + I2S_DMACR, g_aud.i2s_dmacr_reg);
    aud_mmio_wr32(RP1_I2S0_BASE + I2S_CER, 1);              /* Enable BCLK / LRCLK clock generator */
    aud_mmio_wr32(RP1_I2S0_BASE + I2S_ITER, 1);             /* Enable Transmitter */
    aud_mmio_wr32(RP1_I2S0_BASE + I2S_IER, 1);              /* Global I2S Enable */
}

static void i2s_audio_write_frame(int16_t left, int16_t right) {
    /* Scale 16-bit PCM to 24-bit signed left-justified in 32-bit I2S slot for PCM5102A */
    int32_t l24 = ((int32_t)left) << 8;
    int32_t r24 = ((int32_t)right) << 8;

    u32 idx = g_aud.dma_write_idx % AUDIO_DMA_RING_FRAMES;
    g_dma_ring_buf[idx * 2 + 0] = l24;
    g_dma_ring_buf[idx * 2 + 1] = r24;
    g_aud.dma_write_idx++;
    g_aud.dma_read_idx = g_aud.dma_write_idx;               /* Simulated DMA consumer keeps pace */
    g_aud.i2s_samples_pushed += 2;

    aud_mmio_wr32(RP1_I2S0_BASE + I2S_LTHR0, (u32)l24);
    aud_mmio_wr32(RP1_I2S0_BASE + I2S_RTHR0, (u32)r24);
}

/* ============================================================================
 * 3. HDMI Audio Driver (`vc4-hdmi` Architecture for BCM2712 VideoCore VII)
 *    - EDID CEA-861 Short Audio Descriptor (SAD) parser
 *    - HDMI Audio Clock Regeneration (ACR) N / CTS calculation
 *    - CEA-861-D Audio InfoFrame (0x84) builder + checksum + Packet RAM loader
 *    - IEC60958 / S/PDIF Subframe encoder (B/M/W preambles + 24-bit PCM + Channel Status + Parity)
 * ============================================================================ */

/* Standard 256-byte HDMI Sink EDID with Block 1 CEA-861 Extension containing LPCM Audio Data Block */
static const u8 g_default_hdmi_edid[256] = {
    /* Block 0: Base EDID 1.3 (Raspberry Pi HDMI Monitor 1280x720p60) */
    0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x49, 0x10, 0x05, 0x27, 0x01, 0x00, 0x00, 0x00,
    0x01, 0x22, 0x01, 0x03, 0x80, 0x50, 0x2d, 0x78, 0x0a, 0xee, 0x91, 0xa3, 0x54, 0x4c, 0x99, 0x26,
    0x0f, 0x50, 0x54, 0x20, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x1d, 0x00, 0x72, 0x51, 0xd0, 0x1e, 0x20, 0x6e, 0x28,
    0x55, 0x00, 0x20, 0xc2, 0x31, 0x00, 0x00, 0x1e, 0x00, 0x00, 0x00, 0xfc, 0x00, 0x52, 0x50, 0x69,
    0x35, 0x2d, 0x48, 0x44, 0x4d, 0x49, 0x2d, 0x54, 0x56, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x4b,
    /* Block 1: CEA-861-D Extension Block (Tag 0x02, Revision 0x03, Basic Audio Bit 6 = 1) */
    0x02, 0x03, 0x18, 0x71,
    /* Audio Data Block (Tag=1 [bits 7:5 = 001], Length=3): LPCM (Format=1), 2ch, 32/44.1/48kHz, 16/20/24-bit */
    0x23, 0x09, 0x07, 0x07,
    /* Speaker Allocation Data Block (Tag=4 [bits 7:5 = 100], Length=3): FL/FR stereo */
    0x83, 0x01, 0x00, 0x00,
    /* Vendor Specific Data Block (HDMI Licensing LLC 0x000C03) */
    0x67, 0x03, 0x0c, 0x00, 0x10, 0x00, 0x00, 0x1e
};

/* Parse CEA-861 Extension Block in EDID to verify Sink LPCM audio support */
static int vc4_hdmi_parse_edid_audio(const u8 *edid, size_t len) {
    if (!edid || len < 256) return -1;
    const u8 *cea = edid + 128;
    if (cea[0] != 0x02 || cea[1] < 0x01) return -1;         /* Must be CEA-861 extension tag 0x02 */
    u8 dtd_offset = cea[2];
    if (dtd_offset < 4 || dtd_offset > 127) return -1;

    u8 pos = 4;
    while (pos < dtd_offset) {
        u8 hdr = cea[pos];
        u8 tag = (hdr >> 5) & 0x07U;
        u8 blk_len = hdr & 0x1fU;
        if (pos + 1 + blk_len > dtd_offset) break;
        if (tag == 1 && blk_len >= 3) {                     /* Audio Data Block (Short Audio Descriptor) */
            g_aud.hdmi_edid_sad[0] = cea[pos + 1];
            g_aud.hdmi_edid_sad[1] = cea[pos + 2];
            g_aud.hdmi_edid_sad[2] = cea[pos + 3];
            g_aud.hdmi_edid_valid  = 1;
            return 0;
        }
        pos += 1 + blk_len;
    }
    return -1;
}

/*
 * Compute HDMI Audio Clock Regeneration (ACR) N and CTS values per HDMI 1.4b Sec 7.2:
 *   N   = 128 * fs / 1000   (e.g. 6144 for 48 kHz, 4096 for 32 kHz, 6272 for 44.1 kHz)
 *   CTS = (f_tmds * N) / (128 * fs)
 */
static void vc4_hdmi_calc_n_cts(u32 sample_rate, u32 tmds_clock_hz, u32 *out_n, u32 *out_cts) {
    u32 n = 128U * sample_rate / 1000U;
    if (sample_rate == 44100U) n = 6272U;
    else if (sample_rate == 48000U) n = 6144U;
    else if (sample_rate == 32000U) n = 4096U;

    u64 num = (u64)tmds_clock_hz * (u64)n;
    u64 den = 128ULL * (u64)sample_rate;
    u32 cts = (u32)(num / den);

    *out_n   = n;
    *out_cts = cts;
}

/*
 * Build & write CEA-861-D Audio InfoFrame (Packet Type 0x84, Version 0x01, Length 0x0A)
 * into BCM2712 VideoCore VII HDMI Packet RAM (matches `vc4_hdmi_set_audio_infoframe()`).
 *
 * Checksum rule: (HB0 + HB1 + HB2 + Checksum + PB1..PB10) & 0xFF == 0
 */
static void vc4_hdmi_write_audio_infoframe(u32 channels) {
    memset(g_aud.hdmi_infoframe, 0, sizeof(g_aud.hdmi_infoframe));
    g_aud.hdmi_infoframe[0] = 0x84;                         /* HB0: Audio InfoFrame Type */
    g_aud.hdmi_infoframe[1] = 0x01;                         /* HB1: Version 1 */
    g_aud.hdmi_infoframe[2] = 0x0a;                         /* HB2: Length = 10 payload bytes */

    /* PB1: CT[7:4]=0 (Refer to Stream Header), CC[2:0] = (channels - 1) */
    g_aud.hdmi_infoframe[4] = (u8)((channels - 1U) & 0x07U);
    /* PB2: SF[4:2]=0, SS[1:0]=0 (Refer to Stream Header per CEA-861-D Sec 6.6.1) */
    g_aud.hdmi_infoframe[5] = 0x00;
    /* PB3: 0x00 */
    g_aud.hdmi_infoframe[6] = 0x00;
    /* PB4: CA (Channel/Speaker Allocation) = 0x00 for Front Left + Front Right (FL/FR) */
    g_aud.hdmi_infoframe[7] = 0x00;
    /* PB5: LSV/DM_INH = 0x00 (0 dB level shift, downmix permitted) */
    g_aud.hdmi_infoframe[8] = 0x00;

    u8 sum = 0;
    for (size_t i = 0; i < sizeof(g_aud.hdmi_infoframe); i++) {
        if (i != 3) sum = (u8)(sum + g_aud.hdmi_infoframe[i]);
    }
    g_aud.hdmi_infoframe[3] = (u8)(0x100U - sum);           /* PB0: Checksum byte */

    /* Pack into 32-bit words and write to BCM2712 HDMI Packet RAM Slot 4 (Audio InfoFrame) */
    u64 pkt_ram_slot = BCM2712_HDMI0_RAM_BASE + (4U * 0x24U);
    u32 hb_word = (u32)g_aud.hdmi_infoframe[0] |
                  ((u32)g_aud.hdmi_infoframe[1] << 8) |
                  ((u32)g_aud.hdmi_infoframe[2] << 16);
    aud_mmio_wr32(pkt_ram_slot + 0x00, hb_word);

    for (u32 w = 0; w < 3; w++) {
        u32 base = 3U + w * 4U;
        u32 b0 = base + 0 < 14 ? g_aud.hdmi_infoframe[base + 0] : 0;
        u32 b1 = base + 1 < 14 ? g_aud.hdmi_infoframe[base + 1] : 0;
        u32 b2 = base + 2 < 14 ? g_aud.hdmi_infoframe[base + 2] : 0;
        u32 b3 = base + 3 < 14 ? g_aud.hdmi_infoframe[base + 3] : 0;
        u32 pb_word = b0 | (b1 << 8) | (b2 << 16) | (b3 << 24);
        aud_mmio_wr32(pkt_ram_slot + 0x04 + w * 4U, pb_word);
    }

    /* Enable Audio InfoFrame transmission in VC4_HDMI_RAM_PACKET_CONFIG (Bit 4 + Enable Bit 16) */
    g_aud.hdmi_ram_pkt_cfg_reg = (1U << 16) | (1U << 4);
    aud_mmio_wr32(BCM2712_HDMI0_BASE + VC4_HDMI_RAM_PACKET_CONFIG, g_aud.hdmi_ram_pkt_cfg_reg);
}

/* Initialize 192-bit (24-byte) IEC60958 Consumer Channel Status Block for 48 kHz 24-bit LPCM */
static void vc4_hdmi_init_iec60958_cs(u32 sample_rate) {
    memset(g_aud.iec60958_cs, 0, sizeof(g_aud.iec60958_cs));
    g_aud.iec60958_cs[0] = 0x04;                            /* Consumer, Linear PCM, Copy permitted */
    g_aud.iec60958_cs[1] = 0x00;                            /* Category code: General */
    g_aud.iec60958_cs[2] = 0x00;                            /* Source 0, Channel 0 (filled per subframe) */
    if (sample_rate == 44100U)      g_aud.iec60958_cs[3] = 0x00;
    else if (sample_rate == 32000U) g_aud.iec60958_cs[3] = 0x03;
    else                            g_aud.iec60958_cs[3] = 0x02; /* 0x02 = 48 kHz per IEC60958-3 */
    g_aud.iec60958_cs[4] = 0x0b;                            /* Max 24-bit word length, 24-bit active */
}

/*
 * Format one 32-bit IEC60958 subframe for BCM2712 MAI FIFO (`VC4_HD_MAI_DATA`):
 *   Bits [3:0]   : Preamble (B = 0x8 at frame 0 Ch0, M = 0x2 at Ch0, W = 0x4 at Ch1)
 *   Bits [27:4]  : 24-bit Two's Complement Audio Sample
 *   Bit  [28]    : Validity flag (V = 0 for valid linear PCM)
 *   Bit  [29]    : User data bit (U = 0)
 *   Bit  [30]    : Channel status bit (C = bit `frame_idx` of `iec60958_cs`)
 *   Bit  [31]    : Even parity bit (P over bits 4..30)
 */
static u32 vc4_hdmi_pack_iec60958_subframe(int16_t pcm16, u32 channel, u32 frame_idx) {
    u32 preamble = (channel == 0) ? ((frame_idx == 0) ? 0x8U : 0x2U) : 0x4U;
    u32 sample24 = ((u32)((int32_t)pcm16 << 8)) & 0x00ffffffU;
    u32 cs_byte  = g_aud.iec60958_cs[(frame_idx >> 3) % 24U];
    u32 c_bit    = (cs_byte >> (frame_idx & 7U)) & 1U;

    u32 word = preamble | (sample24 << 4) | (c_bit << 30);

    /* Compute even parity over bits [30:4] */
    u32 p_val = word >> 4;
    p_val ^= p_val >> 16;
    p_val ^= p_val >> 8;
    p_val ^= p_val >> 4;
    p_val ^= p_val >> 2;
    p_val ^= p_val >> 1;
    u32 parity = p_val & 1U;

    return word | (parity << 31);
}

static void vc4_hdmi_audio_init(u32 sample_rate) {
    g_aud.hdmi_tmds_clock_hz = 74250000U;                   /* 1280x720p @ 60Hz pixel/TMDS clock = 74.25 MHz */

    /* Step 1: Parse HDMI Sink EDID for CEA-861 Short Audio Descriptors */
    vc4_hdmi_parse_edid_audio(g_default_hdmi_edid, sizeof(g_default_hdmi_edid));

    /* Step 2: Calculate & program Clock Regeneration Packet (ACR) N and CTS */
    vc4_hdmi_calc_n_cts(sample_rate, g_aud.hdmi_tmds_clock_hz,
                        &g_aud.hdmi_acr_n, &g_aud.hdmi_acr_cts);
    g_aud.hdmi_crp_cfg_reg = (1U << 24) | (g_aud.hdmi_acr_n & 0x000fffffU); /* External CTS enable + N */
    aud_mmio_wr32(BCM2712_HDMI0_BASE + VC4_HDMI_CTS_0, g_aud.hdmi_acr_cts);
    aud_mmio_wr32(BCM2712_HDMI0_BASE + VC4_HDMI_CTS_1, g_aud.hdmi_acr_cts);
    aud_mmio_wr32(BCM2712_HDMI0_BASE + VC4_HDMI_CRP_CFG, g_aud.hdmi_crp_cfg_reg);

    /* Step 3: Configure MAI FIFO Thresholds, Format, and Channel Map (FL=0, FR=1) */
    g_aud.hdmi_mai_thr_reg = (0x10U << 24) | (0x10U << 16) | (0x08U << 8) | 0x08U;
    g_aud.hdmi_mai_fmt_reg = (0x02U << 16) | (0x08U << 8);  /* 2-ch LPCM @ 48 kHz */
    g_aud.hdmi_mai_ctl_reg = VC4_HD_MAI_CTL_RESET | VC4_HD_MAI_CTL_ERRORF | VC4_HD_MAI_CTL_UNDERF;
    aud_mmio_wr32(BCM2712_HD_BASE + VC4_HD_MAI_CTL, g_aud.hdmi_mai_ctl_reg);
    aud_mmio_wr32(BCM2712_HD_BASE + VC4_HD_MAI_THR, g_aud.hdmi_mai_thr_reg);
    aud_mmio_wr32(BCM2712_HD_BASE + VC4_HD_MAI_FMT, g_aud.hdmi_mai_fmt_reg);
    aud_mmio_wr32(BCM2712_HDMI0_BASE + VC4_HDMI_MAI_CHANNEL_MAP, 0x00000008U); /* Ch0->FL, Ch1->FR */
    aud_mmio_wr32(BCM2712_HDMI0_BASE + VC4_HDMI_MAI_CONFIG, (1U << 26) | 0x03U);
    aud_mmio_wr32(BCM2712_HDMI0_BASE + VC4_HDMI_AUDIO_PACKET_CONFIG,
                  (1U << 29) | (1U << 28) | (0x01U << 10) | 0x03U);

    /* Step 4: Initialize IEC60958 Channel Status & CEA-861 Audio InfoFrame (0x84) */
    vc4_hdmi_init_iec60958_cs(sample_rate);
    vc4_hdmi_write_audio_infoframe(2);

    /* Step 5: Enable MAI Audio FIFO with 2 channels, Parity generation, and Channel Align */
    g_aud.hdmi_mai_ctl_reg = VC4_HD_MAI_CTL_ENABLE |
                             (2U << VC4_HD_MAI_CTL_CHNUM_SHIFT) |
                             VC4_HD_MAI_CTL_PAREN |
                             VC4_HD_MAI_CTL_Wholsmp |
                             VC4_HD_MAI_CTL_CHALIGN;
    aud_mmio_wr32(BCM2712_HD_BASE + VC4_HD_MAI_CTL, g_aud.hdmi_mai_ctl_reg);
}

static void vc4_hdmi_write_frame(int16_t left, int16_t right) {
    u32 fidx = g_aud.iec60958_frame_idx;
    u32 sub_l = vc4_hdmi_pack_iec60958_subframe(left,  0, fidx);
    u32 sub_r = vc4_hdmi_pack_iec60958_subframe(right, 1, fidx);
    g_aud.iec60958_frame_idx = (fidx + 1U) % 192U;

    g_aud.last_iec_subframe_l = sub_l;
    g_aud.last_iec_subframe_r = sub_r;
    g_aud.hdmi_subframes_pushed += 2;

    aud_mmio_wr32(BCM2712_HD_BASE + VC4_HD_MAI_DATA, sub_l);
    aud_mmio_wr32(BCM2712_HD_BASE + VC4_HD_MAI_DATA, sub_r);
}

/* ============================================================================
 * Public Kernel Audio API & Diagnostic Shell Commands
 * ============================================================================ */
static const char *audio_backend_name(int b) {
    if (b == AUDIO_BACKEND_PWM)  return "RP1 PWM0 Audio (GPIO 12/13, 48kHz Carrier, 11-bit FIFO)";
    if (b == AUDIO_BACKEND_I2S)  return "RP1 DesignWare I2S0 + DMA0 -> PCM5102A DAC (24-bit/48kHz)";
    return "BCM2712 VideoCore VII vc4-hdmi MAI + CEA-861 InfoFrame + IEC60958 (HDMI Audio)";
}

void audio_init_all(void) {
    memset(&g_aud, 0, sizeof(g_aud));
    g_aud.active_backend  = AUDIO_BACKEND_HDMI;
    g_aud.sample_rate     = 48000U;
    g_aud.channels        = 2U;
    g_aud.bits_per_sample = 24U;
    g_aud.start_uptime_ms = uptime_ms();

    pwm_audio_init(48000U);
    i2s_audio_init(48000U);
    vc4_hdmi_audio_init(48000U);

    /* Prime all three pipelines with a short 64-frame startup burst so registers are populated */
    for (u32 i = 0; i < 64; i++) {
        int16_t s = fast_sin_pcm16((i * 256U) / 64U);
        pwm_audio_write_frame(s, s);
        i2s_audio_write_frame(s, s);
        vc4_hdmi_write_frame(s, s);
        g_aud.total_frames_rendered++;
    }
}

void audio_submit_pcm16(const int16_t *samples, u32 frames, u32 sample_rate) {
    (void)sample_rate;
    if (!samples) return;
    for (u32 i = 0; i < frames; i++) {
        int16_t l = samples[i * 2 + 0];
        int16_t r = samples[i * 2 + 1];
        pwm_audio_write_frame(l, r);
        i2s_audio_write_frame(l, r);
        vc4_hdmi_write_frame(l, r);
        g_aud.total_frames_rendered++;
    }
}

/* Audio master clock in microseconds derived from rendered audio frames (48 kHz = 1000000 / 48000 us/frame) */
u64 audio_MasterClock_us(void) {
    return (g_aud.total_frames_rendered * 1000000ULL) / (u64)(g_aud.sample_rate ? g_aud.sample_rate : 48000U);
}

static void audio_run_tone_test(int backend, u32 freq_hz, u32 duration_ms) {
    if (g_aud.sample_rate == 0) audio_init_all();
    g_aud.active_backend = backend;

    u32 total_frames = (g_aud.sample_rate * duration_ms) / 1000U;
    kprintf("\n=== Bare-Metal Audio Tone Generator (%u Hz, %u ms, %u stereo frames) ===\n",
            freq_hz, duration_ms, total_frames);
    kprintf("  Active Backend : %s\n", audio_backend_name(backend));

    u64 t0 = uptime_ms();
    for (u32 n = 0; n < total_frames; n++) {
        u32 phase = (u32)(((u64)n * (u64)freq_hz * 256ULL) / (u64)g_aud.sample_rate);
        int16_t sample_l = fast_sin_pcm16(phase);
        int16_t sample_r = fast_sin_pcm16(phase + 64U);     /* 90-degree stereo quadrature */

        if (backend == AUDIO_BACKEND_PWM) {
            pwm_audio_write_frame(sample_l, sample_r);
        } else if (backend == AUDIO_BACKEND_I2S) {
            i2s_audio_write_frame(sample_l, sample_r);
        } else {
            vc4_hdmi_write_frame(sample_l, sample_r);
        }
        g_aud.total_frames_rendered++;
    }
    u64 dt = uptime_ms() - t0;

    if (backend == AUDIO_BACKEND_PWM) {
        kprintf("  [PWM0 @ 0x1f00098000] GLOBAL_CTRL=0x%08x FIFO_CTRL=0x%08x RANGE=%u ticks\n",
                g_aud.pwm_ctrl_reg, g_aud.pwm_fifo_reg, g_aud.pwm_range);
        kprintf("  [PWM0 Duty] Last L=%u/%u R=%u/%u | Total PWM samples=%lu\n",
                g_aud.pwm_last_duty_l, g_aud.pwm_range,
                g_aud.pwm_last_duty_r, g_aud.pwm_range,
                (unsigned long)g_aud.pwm_samples_pushed);
    } else if (backend == AUDIO_BACKEND_I2S) {
        kprintf("  [I2S0 @ 0x1f000a0000] BCLK=%u Hz CCR=0x%04x TCR0=0x%02x DMACR=0x%08x\n",
                g_aud.i2s_bclk_hz, g_aud.i2s_ccr_reg, g_aud.i2s_tcr0_reg, g_aud.i2s_dmacr_reg);
        kprintf("  [DMA0 Ring] Capacity=%u frames | WriteIdx=%u | Total I2S samples=%lu\n",
                g_aud.dma_ring_frames, g_aud.dma_write_idx, (unsigned long)g_aud.i2s_samples_pushed);
    } else {
        kprintf("  [vc4-hdmi @ 0x107c701400] EDID CEA-861 SAD=[%02x %02x %02x] (LPCM 2ch 48kHz 24-bit OK)\n",
                g_aud.hdmi_edid_sad[0], g_aud.hdmi_edid_sad[1], g_aud.hdmi_edid_sad[2]);
        kprintf("  [HDMI ACR Clock] TMDS=%u Hz | N=%u | CTS=%u | CRP_CFG=0x%08x\n",
                g_aud.hdmi_tmds_clock_hz, g_aud.hdmi_acr_n, g_aud.hdmi_acr_cts, g_aud.hdmi_crp_cfg_reg);
        kprintf("  [CEA-861 InfoFrame 0x84] Header=[%02x %02x %02x] Checksum=0x%02x PB1..4=[%02x %02x %02x %02x]\n",
                g_aud.hdmi_infoframe[0], g_aud.hdmi_infoframe[1], g_aud.hdmi_infoframe[2],
                g_aud.hdmi_infoframe[3], g_aud.hdmi_infoframe[4], g_aud.hdmi_infoframe[5],
                g_aud.hdmi_infoframe[6], g_aud.hdmi_infoframe[7]);
        kprintf("  [IEC60958 MAI FIFO] MAI_CTL=0x%08x Subframe_L=0x%08x Subframe_R=0x%08x | Subframes=%lu\n",
                g_aud.hdmi_mai_ctl_reg, g_aud.last_iec_subframe_l, g_aud.last_iec_subframe_r,
                (unsigned long)g_aud.hdmi_subframes_pushed);
    }
    kprintf("  Completed in %lu ms (Audio Master Clock = %lu us)\n\n",
            (unsigned long)dt, (unsigned long)audio_MasterClock_us());
}

void audio_cmd(const char *arg) {
    if (g_aud.sample_rate == 0) audio_init_all();

    if (!arg || !*arg || !strcmp(arg, "status")) {
        kprintf("\n=== Bare-Metal Audio Subsystem Status (Pi 5 RP1 + BCM2712 vc4-hdmi) ===\n");
        kprintf("  Active Route   : %s\n", audio_backend_name(g_aud.active_backend));
        kprintf("  1. PWM0 Audio  : Base 0x1f00098000 | Carrier=%u Hz (Range=%u) | Samples=%lu\n",
                g_aud.pwm_clock_hz / (g_aud.pwm_range ? g_aud.pwm_range : 1),
                g_aud.pwm_range, (unsigned long)g_aud.pwm_samples_pushed);
        kprintf("  2. I2S0 + DMA0 : Base 0x1f000a0000 | BCLK=%u Hz (64x fs) | Ring=%u frames | Samples=%lu\n",
                g_aud.i2s_bclk_hz, g_aud.dma_ring_frames, (unsigned long)g_aud.i2s_samples_pushed);
        kprintf("  3. vc4-hdmi0   : Base 0x107c701400 | ACR N=%u CTS=%u | InfoFrame 0x84 CS=0x%02x | Subframes=%lu\n",
                g_aud.hdmi_acr_n, g_aud.hdmi_acr_cts, g_aud.hdmi_infoframe[3],
                (unsigned long)g_aud.hdmi_subframes_pushed);
        kprintf("  Master Clock   : %lu us (%lu stereo frames rendered @ %u Hz)\n",
                (unsigned long)audio_MasterClock_us(),
                (unsigned long)g_aud.total_frames_rendered, g_aud.sample_rate);
        kprintf("  Usage          : audio [status | pwm | i2s | hdmi | all]\n\n");
        return;
    }

    if (!strcmp(arg, "pwm")) {
        audio_run_tone_test(AUDIO_BACKEND_PWM, 440U, 100U);
    } else if (!strcmp(arg, "i2s")) {
        audio_run_tone_test(AUDIO_BACKEND_I2S, 880U, 100U);
    } else if (!strcmp(arg, "hdmi")) {
        audio_run_tone_test(AUDIO_BACKEND_HDMI, 1000U, 100U);
    } else if (!strcmp(arg, "all")) {
        audio_run_tone_test(AUDIO_BACKEND_PWM,  440U,  50U);
        audio_run_tone_test(AUDIO_BACKEND_I2S,  880U,  50U);
        audio_run_tone_test(AUDIO_BACKEND_HDMI, 1000U, 50U);
    } else {
        kprintf("  Usage: audio [status | pwm | i2s | hdmi | all]\n");
    }
}

int audio_status_json(char *buf, size_t cap) {
    if (g_aud.sample_rate == 0) audio_init_all();
    return ksnprintf(buf, cap,
        "{"
        "\"active_backend\":\"%s\","
        "\"sample_rate\":%u,"
        "\"channels\":%u,"
        "\"master_clock_us\":%lu,"
        "\"pwm\":{\"base\":\"0x1f00098000\",\"range\":%u,\"ctrl\":\"0x%08x\",\"samples\":%lu},"
        "\"i2s\":{\"base\":\"0x1f000a0000\",\"bclk_hz\":%u,\"dma_ring_frames\":%u,\"samples\":%lu},"
        "\"hdmi\":{\"base\":\"0x107c701400\",\"edid_lpcm_valid\":%d,\"acr_n\":%u,\"acr_cts\":%u,"
        "\"infoframe_checksum\":\"0x%02x\",\"mai_ctl\":\"0x%08x\",\"last_subframe_l\":\"0x%08x\",\"subframes\":%lu}"
        "}\n",
        g_aud.active_backend == AUDIO_BACKEND_PWM ? "pwm" :
        (g_aud.active_backend == AUDIO_BACKEND_I2S ? "i2s" : "vc4-hdmi"),
        g_aud.sample_rate, g_aud.channels, (unsigned long)audio_MasterClock_us(),
        g_aud.pwm_range, g_aud.pwm_ctrl_reg, (unsigned long)g_aud.pwm_samples_pushed,
        g_aud.i2s_bclk_hz, g_aud.dma_ring_frames, (unsigned long)g_aud.i2s_samples_pushed,
        g_aud.hdmi_edid_valid, g_aud.hdmi_acr_n, g_aud.hdmi_acr_cts,
        g_aud.hdmi_infoframe[3], g_aud.hdmi_mai_ctl_reg, g_aud.last_iec_subframe_l,
        (unsigned long)g_aud.hdmi_subframes_pushed);
}
