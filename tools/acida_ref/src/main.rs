//! acida_ref SCRIPT OUT: runs one scenario script on OpenRB-338's engine and writes the
//! output samples as f32 LE to OUT (mono devices: one sample per frame; `delay` and
//! `comp_stereo`: L, R interleaved). `acida_ref --tables OUT` dumps the computed tables
//! instead (the C++ port's are compared against them).
//!
//! The script language (shared with tools/acida_test/acida_test.cpp, which must interpret
//! it identically) is one command per line, `#` starts a comment:
//!
//! ```text
//! device tb303|dist|pcf|comp|comp_stereo|delay   (first command)
//! vintage 0|1          303 sound model
//! param C V            set_param(controller C, value V)
//! on N V / off N       note mode key_on / key_off
//! tempo T              BlockContext tempo (BPM*1000), default 120000
//! tickstep S           ticks added before every block (default 0)
//! input none|saw|noise P AMP   the effects' input: saw period P frames, noise seed P
//! run N                N blocks of 64 frames
//! ```
// SPDX-License-Identifier: GPL-3.0-only
use std::io::Write;

use orb_engine::comp::Compressor;
use orb_engine::delay::Delay;
use orb_engine::dist::Distortion;
use orb_engine::frame::{BLOCK, Block, BlockContext, ChannelDevice, InsertEffect, SendEffect, SongDevice};
use orb_engine::pcf::Pcf;
use orb_engine::process::ProcessState;
use orb_engine::tables::Tables;
use orb_engine::tb303::Tb303;

enum Dev {
    Tb303(Box<Tb303>),
    Dist(Distortion),
    Pcf(Pcf),
    Comp(Compressor),
    CompStereo(Compressor),
    Delay(Box<Delay>),
}

/// The deterministic test input (the same float operations as acida_test).
struct Input {
    kind: u8, // 0 none, 1 saw, 2 noise
    p: u32,
    amp: f32,
    n: u32,
    seed: u32,
}

impl Input {
    fn next(&mut self) -> f32 {
        let v = match self.kind {
            1 => {
                let x = (self.n % self.p) as f32 * (2.0f32 / self.p as f32) - 1.0f32;
                x * self.amp
            }
            2 => {
                self.seed = self.seed.wrapping_mul(1664525).wrapping_add(1013904223);
                let x = (self.seed >> 9) as f32 / 4194304.0f32 - 1.0f32;
                x * self.amp
            }
            _ => 0.0,
        };
        self.n = self.n.wrapping_add(1);
        v
    }
}

fn dump_tables(out: &str) {
    let t = Tables::new();
    let mut v: Vec<f32> = Vec::new();
    v.extend_from_slice(&t.level);
    for a in t.pan.iter() {
        for b in a.iter() {
            v.extend_from_slice(b);
        }
    }
    v.extend_from_slice(&t.tb303_pitch[..]);
    for r in t.tb303_filter.iter() {
        for e in r.iter() {
            v.extend_from_slice(e);
        }
    }
    for tab in [&t.pcf_lowpass, &t.pcf_bandpass] {
        for r in tab.iter() {
            for e in r.iter() {
                v.extend_from_slice(e);
            }
        }
    }
    for r in t.dist_shaper.iter() {
        v.extend_from_slice(r);
    }
    for s in [t.tb303_smoother, t.pcf_smoother] {
        v.extend_from_slice(&[s.b2, s.a, s.g]);
    }
    write_f32(out, &v);
}

fn write_f32(path: &str, v: &[f32]) {
    let mut f = std::io::BufWriter::new(std::fs::File::create(path).expect("create output"));
    for x in v {
        f.write_all(&x.to_le_bytes()).unwrap();
    }
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    if args.len() == 3 && args[1] == "--tables" {
        dump_tables(&args[2]);
        return;
    }
    if args.len() != 3 {
        eprintln!("usage: acida_ref SCRIPT OUT | acida_ref --tables OUT");
        std::process::exit(2);
    }
    let script = std::fs::read_to_string(&args[1]).expect("read script");
    let tables = Tables::new();
    let mut dev: Option<Dev> = None;
    let mut tempo: u32 = 120_000;
    let mut tick: i32 = 0;
    let mut tickstep: i32 = 0;
    let mut input = Input { kind: 0, p: 1, amp: 0.0, n: 0, seed: 0 };
    let mut out: Vec<f32> = Vec::new();
    let mut meter_index: usize = 0;

    for (ln, line) in script.lines().enumerate() {
        let line = line.split('#').next().unwrap().trim();
        if line.is_empty() {
            continue;
        }
        let w: Vec<&str> = line.split_whitespace().collect();
        let num = |i: usize| -> i64 {
            w.get(i)
                .and_then(|s| s.parse::<i64>().ok())
                .unwrap_or_else(|| panic!("line {}: bad number", ln + 1))
        };
        match w[0] {
            "device" => {
                dev = Some(match w[1] {
                    "tb303" => Dev::Tb303(Box::new(Tb303::new())),
                    "dist" => Dev::Dist(Distortion::new()),
                    "pcf" => Dev::Pcf(Pcf::new()),
                    "comp" => Dev::Comp(Compressor::new()),
                    "comp_stereo" => Dev::CompStereo(Compressor::new()),
                    "delay" => Dev::Delay(Box::new(Delay::new(ProcessState::new()))),
                    d => panic!("unknown device {d}"),
                })
            }
            "vintage" => {
                if let Some(Dev::Tb303(t)) = dev.as_mut() {
                    t.set_sound_model(num(1) != 0);
                }
            }
            "param" => {
                let (c, v) = (num(1) as u8, num(2) as u8);
                match dev.as_mut().expect("no device") {
                    Dev::Tb303(t) => t.set_param(c, v),
                    Dev::Dist(d) => d.set_param(c, v),
                    Dev::Pcf(p) => p.set_param(c, v),
                    Dev::Comp(k) | Dev::CompStereo(k) => k.set_param(c, v),
                    Dev::Delay(d) => d.set_param(c, v),
                }
            }
            "on" => {
                if let Some(Dev::Tb303(t)) = dev.as_mut() {
                    t.key_on(num(1) as u8, num(2) as u8);
                }
            }
            "off" => {
                if let Some(Dev::Tb303(t)) = dev.as_mut() {
                    t.key_off(num(1) as u8);
                }
            }
            "tempo" => tempo = num(1) as u32,
            "tickstep" => tickstep = num(1) as i32,
            "input" => {
                input.kind = match w[1] {
                    "none" => 0,
                    "saw" => 1,
                    "noise" => 2,
                    k => panic!("unknown input {k}"),
                };
                if input.kind != 0 {
                    input.p = num(2) as u32;
                    input.amp = w[3].parse::<f32>().expect("amp");
                    if input.kind == 2 {
                        input.seed = input.p;
                    }
                }
            }
            "run" => {
                let d = dev.as_mut().expect("no device");
                for _ in 0..num(1) {
                    tick += tickstep;
                    meter_index = (meter_index + 23) & 63;
                    let ctx = BlockContext {
                        tables: &tables,
                        meter_index,
                        tempo,
                        tick,
                        stack_word: None,
                    };
                    let mut a: Block = [0.0; BLOCK];
                    let mut b: Block = [0.0; BLOCK];
                    for i in 0..BLOCK {
                        a[i] = input.next();
                    }
                    match d {
                        Dev::Tb303(t) => {
                            t.process(&mut a, &ctx);
                            out.extend_from_slice(&a);
                        }
                        Dev::Dist(x) => {
                            x.process(&mut a, &ctx);
                            out.extend_from_slice(&a);
                        }
                        Dev::Pcf(x) => {
                            x.process(&mut a, &ctx);
                            out.extend_from_slice(&a);
                        }
                        Dev::Comp(x) => {
                            x.process(&mut a, &ctx);
                            out.extend_from_slice(&a);
                        }
                        Dev::CompStereo(x) => {
                            // R: the input delayed by half a saw period's worth of samples
                            for i in 0..BLOCK {
                                b[i] = a[BLOCK - 1 - i] * 0.5f32;
                            }
                            x.process_stereo(&mut a, &mut b, &ctx);
                            for i in 0..BLOCK {
                                out.push(a[i]);
                                out.push(b[i]);
                            }
                        }
                        Dev::Delay(x) => {
                            let mut l: Block = [0.0; BLOCK];
                            let mut r: Block = [0.0; BLOCK];
                            x.process(&mut a, &mut l, &mut r, &ctx);
                            for i in 0..BLOCK {
                                out.push(l[i]);
                                out.push(r[i]);
                            }
                        }
                    }
                }
            }
            c => panic!("line {}: unknown command {c}", ln + 1),
        }
    }
    write_f32(&args[2], &out);
}
