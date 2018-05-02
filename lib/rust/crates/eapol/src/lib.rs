// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#![feature(test)]
#![deny(warnings)]

#[macro_use]
extern crate bitfield;
extern crate byteorder;
extern crate bytes;
extern crate failure;
#[macro_use]
extern crate nom;
extern crate test;

use byteorder::BigEndian;
use bytes::{BufMut, Bytes, BytesMut};
use nom::{be_u16, be_u64, be_u8};

pub trait FrameReceiver {
    fn on_eapol_frame(&self, frame: &Frame) -> Result<(), failure::Error>;
}

pub trait KeyFrameReceiver {
    fn on_eapol_key_frame(&self, frame: &KeyFrame) -> Result<(), failure::Error>;
}

#[derive(Debug)]
pub enum Frame {
    Key(KeyFrame)
}

// IEEE Std 802.1X-2010, 11.9, Table 11-5
pub enum KeyDescriptor {
    Rc4 = 1,
    Ieee802dot11 = 2,
}

// IEEE Std 802.1X-2010, 11.3.1
pub enum ProtocolVersion {
    Ieee802dot1x2010 = 3,
}

// IEEE Std 802.1X-2010, 11.3.2, Table 11-3
pub enum PacketType {
    Eap = 0,
    Start = 1,
    Logoff = 2,
    Key = 3,
    AsfAlert = 4,
    Mka = 5,
    AnnouncementGeneric = 6,
    AnnouncementSpecific = 7,
    AnnouncementReq = 8,
}

// IEEE Std 802.11-2016, 12.7.2, Figure 12-33
bitfield! {
    pub struct KeyInformation(u16);
    impl Debug;
    pub key_descriptor_version, set_key_descriptor_version: 2, 0;
    pub key_type, set_key_type: 3, 3;
    // Bit 4-5 reserved.
    pub install, set_install: 6;
    pub key_ack, set_key_ack: 7;
    pub key_mic, set_key_mic: 8;
    pub secure, set_secure: 9;
    pub error, set_error: 10;
    pub request, set_request: 11;
    pub encrypted_key_data, set_encrypted_key_data: 12;
    pub smk_message, set_smk_message: 13;
    value, _: 15,0;
    // Bit 14-15 reserved.
}

impl Default for KeyInformation {
    fn default() -> KeyInformation {
        KeyInformation(0)
    }
}

// IEEE Std 802.11-2016, 12.7.2, Figure 12-32
#[derive(Default, Debug)]
pub struct KeyFrame {
    pub version: u8,
    pub packet_type: u8,
    pub packet_body_len: u16,

    pub descriptor_type: u8,
    pub key_info: KeyInformation,
    pub key_len: u16,
    pub key_replay_counter: u64,
    pub key_nonce: Bytes, /* 32 octets */
    pub key_iv: Bytes, /* 16 octets */
    pub key_rsc: u64,
    // 8 octests reserved.
    pub key_mic: Bytes, /* AKM dependent size */
    pub key_data_len: u16,
    pub key_data: Bytes,
}

impl KeyFrame {
    pub fn to_bytes(&self, clear_mic: bool) -> BytesMut {
        let static_part_len: usize = 35;
        let dynamic_part_len: usize =
            self.key_nonce.len() + self.key_iv.len() + self.key_mic.len() + self.key_data.len();

        let frame_len: usize = static_part_len + dynamic_part_len;
        let mut buf = BytesMut::with_capacity(frame_len);
        buf.put_u8(self.version);
        buf.put_u8(self.packet_type);
        buf.put_u16::<BigEndian>(self.packet_body_len);
        buf.put_u8(self.descriptor_type);
        buf.put_u16::<BigEndian>(self.key_info.value());
        buf.put_u16::<BigEndian>(self.key_len);
        buf.put_u64::<BigEndian>(self.key_replay_counter);
        buf.put_slice(&self.key_nonce[..]);
        buf.put_slice(&self.key_iv[..]);
        buf.put_u64::<BigEndian>(self.key_rsc);
        buf.put_uint::<BigEndian>(0, 8);
        if clear_mic {
            let zeroes: Vec<u8> = vec![0; self.key_mic.len()];
            buf.put_slice(&zeroes[..]);
        } else {
            buf.put_slice(&self.key_mic[..]);
        }
        buf.put_u16::<BigEndian>(self.key_data_len);
        buf.put_slice(&self.key_data[..]);
        buf
    }
}

named_args!(pub key_frame_from_bytes(mic_size: u16) <KeyFrame>,
       do_parse!(
           version: be_u8 >>
           packet_type: verify!(be_u8, |v:u8| v == PacketType::Key as u8) >>
           packet_body_len: be_u16 >>

           descriptor_type: be_u8 >>
           key_info: map!(be_u16, KeyInformation) >>
           key_len: be_u16 >>
           key_replay_counter: be_u64 >>
           key_nonce: take!(32) >>
           key_iv: take!(16) >>
           key_rsc: be_u64 >>
           take!(8 /* reserved octets */) >>
           key_mic: take!(mic_size) >>
           key_data_len: be_u16 >>
           key_data: take!(key_data_len) >>
           eof!() >>
           (KeyFrame{
               version: version,
               packet_type: packet_type,
               packet_body_len: packet_body_len,
               descriptor_type: descriptor_type,
               key_info: key_info,
               key_len: key_len,
               key_replay_counter: key_replay_counter,
               key_mic: Bytes::from(key_mic),
               key_rsc: key_rsc,
               key_iv: Bytes::from(key_iv),
               key_nonce: Bytes::from(key_nonce),
               key_data_len: key_data_len,
               key_data: Bytes::from(key_data),
           })
    )
);

#[cfg(test)]
mod tests {
    use super::*;
    use test::{black_box, Bencher};

    #[bench]
    fn bench_key_frame_from_bytes(b: &mut Bencher) {
        let frame: Vec<u8> = vec![
            0x01, 0x03, 0x00, 0x5f, 0x02, 0x00, 0x8a, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x01, 0x39, 0x5c, 0xc7, 0x6e, 0x1a, 0xe9, 0x9f, 0xa0, 0xb1, 0x22, 0x79,
            0xfe, 0xc3, 0xb9, 0xa9, 0x9e, 0x1d, 0x9a, 0x21, 0xb8, 0x47, 0x51, 0x38, 0x98, 0x25,
            0xf8, 0xc7, 0xca, 0x55, 0x86, 0xbc, 0xda, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x54, 0x01, 0x02, 0x03, 0x01, 0x02, 0x03, 0x01, 0x02, 0x03, 0x01, 0x02, 0x03, 0x01,
            0x02, 0x03, 0x01, 0x02, 0x03, 0x01, 0x02, 0x03, 0x01, 0x02, 0x03, 0x01, 0x02, 0x03,
            0x01, 0x02, 0x03, 0x01, 0x02, 0x03, 0x01, 0x02, 0x03, 0x01, 0x02, 0x03, 0x01, 0x02,
            0x03, 0x01, 0x02, 0x03, 0x01, 0x02, 0x03, 0x01, 0x02, 0x03, 0x01, 0x02, 0x03, 0x01,
            0x02, 0x03, 0x01, 0x02, 0x03, 0x01, 0x02, 0x03, 0x01, 0x02, 0x03, 0x01, 0x02, 0x03,
            0x01, 0x02, 0x03, 0x01, 0x02, 0x03, 0x01, 0x02, 0x03, 0x01, 0x02, 0x03, 0x01, 0x02,
            0x03,
        ];
        b.iter(|| key_frame_from_bytes(&frame, black_box(16)));
    }

    #[test]
    fn test_no_key_frame() {
        let frame: Vec<u8> = vec![
            0x01, 0x01, 0x00, 0x5f, 0x02, 0x00, 0x8a, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x01, 0x39, 0x5c, 0xc7, 0x6e, 0x1a, 0xe9, 0x9f, 0xa0, 0xb1, 0x22, 0x79,
            0xfe, 0xc3, 0xb9, 0xa9, 0x9e, 0x1d, 0x9a, 0x21, 0xb8, 0x47, 0x51, 0x38, 0x98, 0x25,
            0xf8, 0xc7, 0xca, 0x55, 0x86, 0xbc, 0xda, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00,
        ];
        let result = key_frame_from_bytes(&frame, 16);
        assert_eq!(result.is_done(), false);
    }

    #[test]
    fn test_too_long() {
        let frame: Vec<u8> = vec![
            0x01, 0x03, 0x00, 0x5f, 0x02, 0x00, 0x8a, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x01, 0x39, 0x5c, 0xc7, 0x6e, 0x1a, 0xe9, 0x9f, 0xa0, 0xb1, 0x22, 0x79,
            0xfe, 0xc3, 0xb9, 0xa9, 0x9e, 0x1d, 0x9a, 0x21, 0xb8, 0x47, 0x51, 0x38, 0x98, 0x25,
            0xf8, 0xc7, 0xca, 0x55, 0x86, 0xbc, 0xda, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x03, 0x01, 0x02, 0x03, 0x04,
        ];
        let result = key_frame_from_bytes(&frame, 16);
        assert_eq!(result.is_done(), false);
    }

    #[test]
    fn test_too_short() {
        let frame: Vec<u8> = vec![
            0x01, 0x03, 0x00, 0x5f, 0x02, 0x00, 0x8a, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x01, 0x39, 0x5c, 0xc7, 0x6e, 0x1a, 0xe9, 0x9f, 0xa0, 0xb1, 0x22, 0x79,
            0xfe, 0xc3, 0xb9, 0xa9, 0x9e, 0x1d, 0x9a, 0x21, 0xb8, 0x47, 0x51, 0x38, 0x98, 0x25,
            0xf8, 0xc7, 0xca, 0x55, 0x86, 0xbc, 0xda, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x03, 0x01,
        ];
        let result = key_frame_from_bytes(&frame, 16);
        assert_eq!(result.is_done(), false);
    }

    #[test]
    fn test_dynamic_mic_size() {
        let frame: Vec<u8> = vec![
            0x01, 0x03, 0x00, 0x6f, 0x02, 0x00, 0x8a, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
            0x07, 0x08, 0x00, 0x00, 0x01, 0x39, 0x5c, 0xc7, 0x6e, 0x1a, 0xe9, 0x9f, 0xa0, 0xb1,
            0x22, 0x79, 0xfe, 0xc3, 0xb9, 0xa9, 0x9e, 0x1d, 0x9a, 0x21, 0xb8, 0x47, 0x51, 0x38,
            0x98, 0x25, 0xf8, 0xc7, 0xca, 0x55, 0x86, 0xbc, 0xda, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x03, 0x01, 0x02, 0x03,
        ];
        let result = key_frame_from_bytes(&frame, 32);
        assert_eq!(result.is_done(), true);
    }

    #[test]
    fn test_to_bytes() {
        let frame: Vec<u8> = vec![
            0x01, 0x03, 0x00, 0x5f, 0x02, 0x00, 0x8a, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x01, 0x39, 0x5c, 0xc7, 0x6e, 0x1a, 0xe9, 0x9f, 0xa0, 0xb1, 0x22, 0x79,
            0xfe, 0xc3, 0xb9, 0xa9, 0x9e, 0x1d, 0x9a, 0x21, 0xb8, 0x47, 0x51, 0x38, 0x98, 0x25,
            0xf8, 0xc7, 0xca, 0x55, 0x86, 0xbc, 0xda, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x03, 0x01, 0x02, 0x03,
        ];
        let result = key_frame_from_bytes(&frame, 16);
        assert_eq!(result.is_done(), true);
        let keyframe: KeyFrame = result.unwrap().1;
        assert_eq!(frame, keyframe.to_bytes(false));
    }

    #[test]
    fn test_to_bytes_dynamic_mic_size() {
        let frame: Vec<u8> = vec![
            0x01, 0x03, 0x00, 0x6f, 0x02, 0x00, 0x8a, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
            0x07, 0x08, 0x00, 0x00, 0x01, 0x39, 0x5c, 0xc7, 0x6e, 0x1a, 0xe9, 0x9f, 0xa0, 0xb1,
            0x22, 0x79, 0xfe, 0xc3, 0xb9, 0xa9, 0x9e, 0x1d, 0x9a, 0x21, 0xb8, 0x47, 0x51, 0x38,
            0x98, 0x25, 0xf8, 0xc7, 0xca, 0x55, 0x86, 0xbc, 0xda, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x03, 0x01, 0x02, 0x03,
        ];
        let result = key_frame_from_bytes(&frame, 32);
        assert_eq!(result.is_done(), true);
        let keyframe: KeyFrame = result.unwrap().1;
        assert_eq!(frame, keyframe.to_bytes(false));
    }

    #[test]
    fn test_to_bytes_clear_mic() {
        #[cfg_attr(rustfmt, rustfmt_skip)]
        let frame: Vec<u8> = vec![
            0x01, 0x03, 0x00, 0x5f, 0x02, 0x00, 0x8a, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x01, 0x39, 0x5c, 0xc7, 0x6e, 0x1a, 0xe9, 0x9f, 0xa0, 0xb1, 0x22, 0x79,
            0xfe, 0xc3, 0xb9, 0xa9, 0x9e, 0x1d, 0x9a, 0x21, 0xb8, 0x47, 0x51, 0x38, 0x98, 0x25,
            0xf8, 0xc7, 0xca, 0x55, 0x86, 0xbc, 0xda, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            // MIC
            0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E,
            0x0F, 0x10,
            0x00, 0x03, 0x01, 0x02, 0x03,
        ];
        let result = key_frame_from_bytes(&frame, 16);
        assert_eq!(result.is_done(), true);
        let keyframe: KeyFrame = result.unwrap().1;

        #[cfg_attr(rustfmt, rustfmt_skip)]
        let expected: Vec<u8> = vec![
            0x01, 0x03, 0x00, 0x5f, 0x02, 0x00, 0x8a, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x01, 0x39, 0x5c, 0xc7, 0x6e, 0x1a, 0xe9, 0x9f, 0xa0, 0xb1, 0x22, 0x79,
            0xfe, 0xc3, 0xb9, 0xa9, 0x9e, 0x1d, 0x9a, 0x21, 0xb8, 0x47, 0x51, 0x38, 0x98, 0x25,
            0xf8, 0xc7, 0xca, 0x55, 0x86, 0xbc, 0xda, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            // Cleared MIC
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00,
            0x00, 0x03, 0x01, 0x02, 0x03,
        ];
        assert_eq!(expected, keyframe.to_bytes(true));
    }

    #[test]
    fn test_correct_packet() {
        let frame: Vec<u8> = vec![
            0x01, 0x03, 0x00, 0x5f, 0x02, 0x00, 0x8a, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x01, 0x39, 0x5c, 0xc7, 0x6e, 0x1a, 0xe9, 0x9f, 0xa0, 0xb1, 0x22, 0x79,
            0xfe, 0xc3, 0xb9, 0xa9, 0x9e, 0x1d, 0x9a, 0x21, 0xb8, 0x47, 0x51, 0x38, 0x98, 0x25,
            0xf8, 0xc7, 0xca, 0x55, 0x86, 0xbc, 0xda, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x03, 0x01, 0x02, 0x03,
        ];
        let result = key_frame_from_bytes(&frame, 16);
        assert_eq!(result.is_done(), true);
        let keyframe: KeyFrame = result.unwrap().1;
        assert_eq!(keyframe.version, 1);
        assert_eq!(keyframe.packet_type, 3);
        assert_eq!(keyframe.packet_body_len, 95);
        assert_eq!(keyframe.descriptor_type, 2);
        assert_eq!(keyframe.key_info.value(), 0x008a);
        assert_eq!(keyframe.key_info.key_descriptor_version(), 2);
        assert_eq!(keyframe.key_info.key_ack(), true);
        assert_eq!(keyframe.key_len, 16);
        assert_eq!(keyframe.key_replay_counter, 1);
        let nonce: Vec<u8> = vec![
            0x39, 0x5c, 0xc7, 0x6e, 0x1a, 0xe9, 0x9f, 0xa0, 0xb1, 0x22, 0x79, 0xfe, 0xc3, 0xb9,
            0xa9, 0x9e, 0x1d, 0x9a, 0x21, 0xb8, 0x47, 0x51, 0x38, 0x98, 0x25, 0xf8, 0xc7, 0xca,
            0x55, 0x86, 0xbc, 0xda,
        ];
        assert_eq!(&keyframe.key_nonce[..], &nonce[..]);
        assert_eq!(keyframe.key_rsc, 0);
        let mic = [0; 16];
        assert_eq!(&keyframe.key_mic[..], mic);
        assert_eq!(keyframe.key_data_len, 3);
        let data: Vec<u8> = vec![0x01, 0x02, 0x03];
        assert_eq!(&keyframe.key_data[..], &data[..]);
    }
}
