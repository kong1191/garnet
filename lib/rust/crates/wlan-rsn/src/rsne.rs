// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use akm;
use bytes::Bytes;
use cipher;
use pmkid;
use suite_selector;

use nom::{IResult, le_u16, le_u8};

macro_rules! if_remaining (
  ($i:expr, $f:expr) => ( cond!($i, $i.len() !=0, call!($f)); );
);

pub const ID: u8 = 48;

// IEEE 802.11-2016, 9.4.2.25.1
#[derive(Default, Debug)]
pub struct Rsne {
    pub element_id: u8,
    pub length: u8,
    pub version: u16,
    pub group_data_cipher_suite: Option<cipher::Cipher>,
    pub pairwise_cipher_suites: Vec<cipher::Cipher>,
    pub akm_suites: Vec<akm::Akm>,
    pub rsn_capabilities: u16,
    pub pmkids: Vec<pmkid::Pmkid>,
    pub group_mgmt_cipher_suite: Option<cipher::Cipher>,
}

fn read_suite_selector<'a, T>(input: &'a [u8]) -> IResult<&'a [u8], T>
where
    T: suite_selector::Factory<Suite = T>,
{
    let (i1, bytes) = try_parse!(input, take!(4));
    let oui = Bytes::from(&bytes[0..3]);
    let (i2, ctor_result) = try_parse!(i1, expr_res!(T::new(oui, bytes[3])));
    return IResult::Done(i2, ctor_result);
}

fn read_pmkid<'a>(input: &'a [u8]) -> IResult<&'a [u8], pmkid::Pmkid> {
    let (i1, bytes) = try_parse!(input, take!(16));
    let pmkid_data = Bytes::from(bytes);
    let (i2, result) = try_parse!(i1, expr_res!(pmkid::new(pmkid_data)));
    return IResult::Done(i2, result);
}

named!(akm<&[u8], akm::Akm>, call!(read_suite_selector::<akm::Akm>));
named!(cipher<&[u8], cipher::Cipher>, call!(read_suite_selector::<cipher::Cipher>));

named!(pub from_bytes<&[u8], Rsne>,
       do_parse!(
           element_id: le_u8 >>
           length: le_u8 >>
           version: le_u16 >>
           group_cipher: if_remaining!(cipher) >>
           pairwise_count: if_remaining!(le_u16) >>
           pairwise_list: count!(cipher, pairwise_count.unwrap_or(0) as usize)  >>
           akm_count: if_remaining!(le_u16) >>
           akm_list: count!(akm, akm_count.unwrap_or(0) as usize)  >>
           rsn_capabilities: if_remaining!(le_u16) >>
           pmkid_count: if_remaining!(le_u16) >>
           pmkid_list: count!(read_pmkid, pmkid_count.unwrap_or(0) as usize)  >>
           group_mgmt_cipher_suite: if_remaining!(cipher) >>
           eof!() >>
           (Rsne{
                element_id: element_id,
                length: length,
                version: version,
                group_data_cipher_suite: group_cipher,
                pairwise_cipher_suites: pairwise_list,
                akm_suites: akm_list,
                rsn_capabilities: rsn_capabilities.unwrap_or(0),
                pmkids: pmkid_list,
                group_mgmt_cipher_suite: group_mgmt_cipher_suite
           })
    )
);

#[cfg(test)]
mod tests {
    use super::*;
    use test::Bencher;

    #[bench]
    fn bench_parse_with_nom(b: &mut Bencher) {
        let frame: Vec<u8> = vec![
            0x30, 0x14, 0x01, 0x00, 0x00, 0x0f, 0xac, 0x04, 0x01, 0x00, 0x00, 0x0f, 0xac, 0x04,
            0x01, 0x00, 0x00, 0x0f, 0xac, 0x02, 0x00, 0x00, 0x01, 0x00, 0x01, 0x02, 0x03, 0x04,
            0x05, 0x06, 0x07, 0x08, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x00, 0x0f,
            0xac, 0x04,
        ];
        b.iter(|| from_bytes(&frame));
    }

    // TODO(hahnr): Add tests.
}
