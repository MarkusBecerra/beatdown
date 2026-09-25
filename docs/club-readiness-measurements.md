> Measurement record behind README.md's "Club-ready?" section. Produced during v1 execution with the binary at commit 168eae7 (before per-file content verification was added; the audio path is unchanged since). The generated test audio, tools and raw result files lived in a temporary session folder and were not kept; the paths under "Method" are historical.

# Are beatdown's conversions club-ready? Measurement report

Binary under test: `beatdown-168eae7` (beatdown 0.1.0, LAME 3.100, default settings: 320 kbps CBR MP3; and `--format flac`).
Date: 2026-09-23. All numbers below were measured on synthetic test material (described under Method). Anything marked **(judgement)** is my opinion as an engineer, not a measurement.

## Verdict

**Yes, the files are club-ready.** The FLACs are bit-for-bit copies of your WAVs. The 320 kbps MP3s were transparent on every test: the level doesn't change (0.000 dB), the response is flat to 20 kHz (within ±0.03 dB on a sine sweep, within 0.1 dB on pink noise) and then cuts off around 20.2 kHz, the stereo image is unchanged, the timing is constant, and the coding noise sits about 33 dB below the music. **(judgement)** Played on a club system, the MP3 should sound the same as your WAV.

**There is one side effect.** Your masters peak at −0.1 dBFS, and the decoded MP3 briefly goes past full scale. The realistic loud test master (−7 LUFS, +0.75 dBTP) produced 888 over-full-scale samples in 30 s. That is 0.03 % of all samples, and the largest over was +0.48 dB. A decoder that outputs fixed-point samples clips these overs. Apple's decoder on this Mac is one such decoder: its output is 16-bit and cut off at full scale. The clipping this caused was 67 dB below the music (59 dB in the worst 0.4 s), about 30 dB quieter than the MP3's own coding noise. **(judgement)** You won't hear it on a club system. The same master limited to −1 dBFS or −1 dBTP produced no overs at all.

## Numbers

Unless noted, "MP3" means the beatdown MP3 decoded with LAME's own decoder in unclipped float, so any overs stay visible. Apple's decoder (AudioToolbox via `afconvert -d LEF32`) was used as the second decoder. **Its output is 16-bit and cut off at full scale:** 100 % of its samples sit on the 16-bit grid, the maximum is 32767/32768 and the minimum is −1.0. Apart from that clipping, the two decoders agree to within −82 dBFS.

### Main test master: 48 kHz / 24-bit house loop at 124 BPM, limited to −0.1 dBFS

| Check | Source (WAV) | MP3 | Result |
|---|---|---|---|
| RMS, L / R | −7.232 / −7.232 dBFS | −7.232 / −7.232 dBFS | **0.000 dB** change (least-squares gain −0.003 dB) |
| Integrated loudness | −7.00 LUFS (max short-term −6.77) | −7.00 LUFS | +0.002 LU |
| Sample peak | −0.100 dBFS | **+0.477 dBFS** | exceeds full scale |
| True peak (4× oversampled) | **+0.754 dBTP** | +0.830 dBTP | the WAV already has overs between samples |
| Samples above 0 dBFS (30 s, both channels) | 0 | **888** (0.031 %): 232 above +0.1 dB, 24 above +0.25 dB, 0 above +0.5 dB | runs of 1–4 samples; 221 clusters (10 ms), about 7 per second |
| Error if a fixed-point decoder hard-clips at 0 dBFS | — | −73.9 dBFS RMS = **66.7 dB below the music**; worst 400 ms block **58.7 dB below** | compare the null residual below |
| Null residual (MP3 − WAV, sample-aligned) | — | −39.5 dBFS = **32.3 dB below the music**; 20 Hz–16 kHz: 33.3 dB below | normal 320 kbps coding noise. Per 1/3 octave it is 29–51 dB below the music under 1 kHz, 17–26 dB below from 2 to 12.5 kHz, 14 dB below at 16 kHz |
| L/R correlation | 0.9707 | 0.9705 | image kept |
| Side/mid energy | −18.28 dB | −18.25 dB | per octave from 125 Hz to 16 kHz: within 0.13 dB |
| 1/3-octave response, 20 Hz–12.5 kHz | — | max deviation 0.03 dB | the 16 kHz band is +0.04 dB |

### Peaks and overs across masters

"Turned down" rows simulate an opt-in pre-encode gain reduction. Master B is a deliberately extreme master: bright, clipped without oversampling, −6 LUFS, +3.7 dBTP, with 2.2 % of its energy above 20 kHz.

| Source | Sample peak | True peak | Loudness | MP3 peak | Samples > 0 dBFS in 30 s | Clip error vs music (average / worst 0.4 s) |
|---|---|---|---|---|---|---|
| A, −0.1 dBFS | −0.10 | +0.75 dBTP | −7.00 LUFS | **+0.48 dBFS** | **888** | −66.7 / −58.7 dB |
| A, −1.0 dBFS sample-peak limit | −1.00 | +0.09 | −7.71 | **−0.47** | **0** | — |
| A, −1.0 dBTP true-peak limit | −1.01 | −1.01 | −7.78 | −0.55 | 0 | — |
| A turned down 0.5 dB | −0.60 | +0.25 | −7.50 | −0.04 | 0 | — |
| A turned down 1.0 / 1.5 / 2.0 dB | −1.10 / −1.60 / −2.10 | −0.25 / −0.75 / −1.25 | −8.0 / −8.5 / −9.0 | −0.39 / −1.05 / −1.61 | 0 / 0 / 0 | — |
| A at 44.1 kHz / 16-bit (CD-style) | −0.10 | +0.89 | −7.00 | +0.37 | 682 | −67.6 / −58.9 dB |
| B, −0.1 dBFS (extreme) | −0.10 | +3.67 | −6.00 | **+2.30** | **9,713** (0.34 %), 304 above +1 dB | −43.7 / −39.9 dB |
| B, −1.0 dBTP | −1.01 | −1.01 | −7.93 | +0.71 | 2 | −76.0 / −57.7 dB |
| B turned down 1 / 2 / 2.5 / 3 dB | | | | +1.64 / +0.65 / +0.14 / −0.81 | 294 / 2 / 1 / 0 | |

LAME's own decode-on-the-fly peak detection (`lame_set_decode_on_the_fly`, same settings as beatdown) reported the same MP3 peaks: +0.477, −0.471, +2.297, +0.367 and +0.705 dBFS for A −0.1, A −1.0, B −0.1, A 44.1k and B −1 dBTP. Its suggested gain changes were +0.5, −0.4, +2.3, +0.4 and +0.8 dB.

### Frequency response (MP3 ÷ source)

| Test signal | Deviation below 16 kHz | 16 kHz band | −1 dB point | −3 dB point | Above that |
|---|---|---|---|---|---|
| Log sweep 20 Hz–23.5 kHz, −3 dBFS, 48 kHz (1/3 oct.; tracked in 43 ms windows) | ≤ 0.001 dB (1/3 oct.); tracked 25 Hz–20 kHz ±0.002 dB (LAME decoder) / ±0.024 dB (Apple) | 0.00 dB | 20.23 kHz | 20.29 kHz | −22 to −24 dB at 20.5 kHz, ≤ −95 dB from 20.75 kHz |
| Pink noise −14 dBFS RMS, independent L/R | 0.07 dB (bands 20 Hz–12.5 kHz) | +0.10 dB | 20.15 kHz | 20.29 kHz | 20 kHz band −2.75 dB |
| Master A (music) | 0.03 dB | +0.04 dB | 20.0 kHz | 20.3 kHz | |
| Log sweep, 44.1 kHz source (MP3 stays 44.1 kHz) | ±0.002 dB to 19.5 kHz | 0.00 dB | 19.93–19.98 kHz | 20.03–20.04 kHz | about −98 dB at 20.5 kHz |
| Log sweep 20 Hz–40 kHz, 96 kHz source (LAME resamples to 48 kHz) | ±0.004 dB to 16 kHz; −0.10 dB at 18 kHz, −0.28 dB at 19 kHz, −0.68 dB at 20 kHz | 0.00 dB | 20.1 kHz | 20.27–20.28 kHz | |

The LAME tag in every MP3 records a 20,500 Hz lowpass. Removing the content above ~20.3 kHz is also why broadband signals lose a little RMS: pink noise −0.084 dB, the sweep −0.091 dB, master B −0.087 dB. Master A lost 0.000 dB.

### Everything else

| Check | Result |
|---|---|
| Timing, Apple decoder | **0 samples** offset. The decoded length equals the source exactly (1,440,000 frames), because it reads LAME's gapless tag (576 samples priming, 576–1344 samples padding). |
| Timing, LAME decoder (ignores the gapless tag) | **1105 samples late** (576 encoder + 529 decoder delay) = **23.0 ms at 48 kHz, 25.1 ms at 44.1 kHz**. Same value in every file measured (18 of 20; the two pure 1 kHz tones can't be aligned unambiguously), and within ±0.003 samples from start to end of each music file, so it doesn't drift. |
| THD+N, 1 kHz sine at −1 dBFS | −89.5 dB (0.0034 %) with the LAME decoder; −87.9 dB with Apple's. Source: −140.5 dB. |
| THD+N, 1 kHz sine at −20 dBFS | −90.1 dB relative to the tone (LAME decoder). Apple's decoder measured −79.5 dB, set by its 16-bit output rather than the MP3. |
| Aliasing, 96→48 kHz (sweep above 24.5 kHz) | Everything in the output under 20 kHz is alias. Overall it measured −42.2 dBFS RMS, **36 dB below the sweep**. By input frequency: 24.5–26.5 kHz input gives nothing (aliases land above the lowpass); **26.5–30.5 kHz input aliases to 17.5–20.5 kHz at only 32–33 dB below the input**; 30.5–32.5 kHz gives 55 dB below; above 32.5 kHz gives 77–84 dB below. |
| FLAC | **20/20 files bit-identical** to the source: sample count, rate, bit depth and every sample. Tested at 16-bit/44.1 kHz, 24-bit/44.1 kHz, 24-bit/48 kHz and 24-bit/96 kHz (96 kHz stays 96 kHz). |
| What rekordbox will read (`afinfo`) | "2 ch, 48000 Hz, .mp3", "bit rate: 320000 bits per second", "1440000 valid frames + 576 priming + 576 remainder". `afinfo` doesn't show the channel mode, so I parsed the frame headers: all 1251 audio frames are MPEG-1 Layer III at 320 kbps and 48 kHz, joint stereo, with M/S used in 861 frames (L/R in 390). The first frame is a LAME 3.100 "Info" (CBR) tag. The ID3 tag is ID3v2.3.0 with TIT2/TPE1/TSSE, but only when the source WAV has tags. |

## Risks and recommendations

### 1. Decoded MP3s of −0.1 dBFS masters go over full scale

**What I measured:** yes, they exceed full scale. The realistic master went +0.48 dB over at most, on 888 samples per 30 s (0.03 %). The overs come in bursts of 1–4 samples, about 7 clusters per second. 78 % of them land on the kick, and most of the rest on the off-beat bass and open-hat hit. The extreme master reached +2.3 dB, on 9,713 samples per 30 s (0.34 %). The CD-style 44.1 kHz/16-bit copy reached +0.37 dB on 682 samples.

The overs are not new peaks created by the MP3. The WAV already reaches +0.75 dBTP between its samples, and the MP3 (+0.83 dBTP) turns those between-sample peaks into sample values. Apple's decoder clips them at full scale. A CDJ that decodes to fixed-point would do the same, but I could not test a CDJ.

**Is it audible on a club system?** **(judgement)** For normal loud masters, no. The measured clip error is 59–67 dB below the music. It sits exactly on the loudest transients, where masking is strongest. It is also roughly 25–35 dB quieter than the MP3's own coding noise (33 dB below the music), which is inaudible at 320 kbps. The extreme master's clip error, 40–44 dB below the music, is still 13–17 dB under that master's own coding noise. That master's real problem is its +3.7 dBTP peaks, which also stress a DAC playing the WAV or FLAC.

**Options:**
- **Leave it as it is.** **(judgement)** This is effectively the norm: DJ-store 320 kbps MP3s are made from masters peaking near 0 dBFS without extra gain, so playback gear sees overs like these all the time. The measured cost is ≤0.5 dB hard clips on 0.03 % of samples, for a master like A.
- **Add an opt-in pre-encode headroom of N dB to beatdown.** For master A, 0.5 dB already removed every over (with 0.04 dB to spare), and **1.0 dB left 0.39 dB of margin**. The extreme master needed **3.0 dB**. The cost is a level drop of N dB that the DJ trims back on the mixer.
  - A smarter variant: beatdown could turn on LAME's decode-on-the-fly peak detection. It reports each file's MP3 peak exactly: it matched my decoder to 0.001 dB on all five files I tested. beatdown could then warn per file, or re-encode only the files that clip, using LAME's suggested gain plus a margin of about 0.5–1 dB. LAME's estimate was 0.1–0.7 dB short on master B (it suggested 2.3 dB; the re-encode was still over at 2.5 dB and clean at 3.0 dB).
- **Ask the producer for a −1 dBTP export.** This removed all overs from master A (MP3 peak −0.55 dBFS) at a cost of 0.78 LU of loudness. A plain −1 dBFS sample-peak limit also worked for A (−0.47 dBFS, cost 0.71 LU). Streaming platforms and AES guidance already ask for −1 dBTP (not measured here), so the master works everywhere. It is not an absolute guarantee: the extreme master B at −1 dBTP still gave 2 overs of +0.7 dB, because much of its energy sits above 20 kHz, where the MP3 removes it.

**My recommendation (judgement):** if the producer is happy to deliver −1 dBTP masters, that is the cleanest fix. Otherwise, leaving things as they are is acceptable for club use. For beatdown itself, a per-file "MP3 peak +x.x dBFS" warning would be cheap and useful, and could come before any automatic gain.

### 2. Start offset differs between decoders (not tested on rekordbox or a CDJ)
The offset is either 0 or 23 ms (1105 samples at 48 kHz), depending on whether the decoder reads LAME's gapless tag. It is constant within a file, so beatgrids won't drift. But if rekordbox and a CDJ handle the tag differently, cues and grids will be 23 ms apart, enough to flam kicks when using sync or quantize. **Check once:** put a hot cue exactly on a kick in rekordbox, export to USB, and confirm the cue lands on the transient on the CDJ.

### 3. High-frequency cut at about 20.2 kHz
**(judgement)** You won't hear it. Adult hearing and most club PA horns stop below that, and FLAC keeps the full band if you want it.

### 4. Sources above 48 kHz
These don't affect your 48 kHz masters. A 96 kHz source loses up to 0.7 dB between 19 and 20 kHz. Ultrasonic content at 26.5–30.5 kHz folds down to 17.5–20.5 kHz at only 32 dB below its own level. **(judgement)** That is inaudible for normal music, where content in that range is far below full level.

### 5. Not measured
- The CDJ's decoder (word length, headroom) and how rekordbox's audio engine handles overs.
- Auto Gain behaviour.
- Whether your specific CDJ model plays 48 kHz MP3. **(judgement)** To my knowledge Pioneer lists 32/44.1/48 kHz MP3 support, but I didn't verify or test it.
- Real masters. The over counts scale with how much of a track sits at the limiter ceiling and how bright or clipped it is, so expect numbers between masters A and B.

### 6. Tags (not an audio issue)
An untagged WAV produces an MP3 with no ID3 tag at all, so the CDJ will show the file name. `--tag-from-name` or tagging the WAVs fixes this.

## Method

- **Test material** (`src/`, `src_extra/`, 30 s unless noted), made with `tools/gen`:
  - **Master A:** a 124 BPM house loop with a 150→48 Hz kick, off-beat sub bass, clap on 2 and 4, 16th hats (high-passed noise, L/R correlation about 0.8, rolled off at 16 kHz) and a detuned-saw pad side-chained to the kick. The chain was a 4×-oversampled soft clipper (+2 dB ceiling), then a look-ahead limiter (1.5 ms look-ahead, 60 ms release) at −0.1 or −1.0 dBFS, or −1.0 dBTP.
  - **Master B:** the same loop with brighter, full-band hats and a clipper without oversampling.
  - Log sweeps at 48, 44.1 and 96 kHz; 20 s of FFT-generated pink noise; 1 kHz sines at −1 and −20 dBFS.
  - Masters are TPDF-dithered to 24 bits, or 16 bits for the CD-style copy.
- **Conversion:** `beatdown-168eae7 src out_mp3` and `--format flac`.
- **Decoding:** LAME 3.100's own decoder (`hip_decode1_unclipped`, float, unclipped; `tools/hipdec`), plus Apple's `afconvert -f WAVE -d LEF32`.
- **Measurement:** `tools/analyze` with the modes `pair`, `sweep`, `resamp`, `thdn` and `flac`. Methods:
  - Offset by FFT cross-correlation at 5 positions per file.
  - True peak with a 4× Kaiser-windowed-sinc interpolator (64 taps per phase).
  - BS.1770-4 gated loudness.
  - Welch spectra for 1/3-octave and 1/24-octave ratios.
  - The sweep response tracked in 43 ms windows.
  - THD+N by least-squares removal of the 1 kHz tone.
  - Clip error as clamp(decoded) − decoded.
- **Meter checks:** the true-peak meter read −6.021 dBTP on faded sines of known −6.021 dBTP at 1, 11, 12 and 20 kHz. The loudness meter read −19.99 LUFS for a −20 dBFS stereo 1 kHz sine.
- **Raw outputs:** `results/`. `run_all.sh` re-runs the main set; the `src_extra/` files (44.1 kHz sweep, master B variants) went through the same tools, and `tools/lameclip` produced the LAME decode-on-the-fly cross-check. All paths are under `/private/tmp/claude-501/-Users-markus-Documents-Coding-beatdown/68f0b517-bcc7-426d-a72f-8e6c087aa855/scratchpad/club/`.
