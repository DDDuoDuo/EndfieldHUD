#!/usr/bin/env python3
"""Synthetic raw-target comparison regressions, with no app or capture API."""
import hashlib
import importlib.util
import json
import math
import pathlib
import sys
import tempfile
import unittest

path=pathlib.Path(__file__).resolve().parents[1]/'tools/compare_source_frames.py'
spec=importlib.util.spec_from_file_location('compare_source_frames',path)
comparison=importlib.util.module_from_spec(spec);sys.modules[spec.name]=comparison;spec.loader.exec_module(comparison)
Frame=comparison.Frame


class RawComparisonTests(unittest.TestCase):
    def test_padding_is_excluded(self):
        a=Frame(bytes([10,20,30,255,0,0,0,0,99,99,99,99]),1,2,6)
        b=Frame(bytes([10,20,30,255,255,255,0,0,99,99,0,0]),1,2,6)
        result=comparison.compare(a,b)
        self.assertEqual(result['allChannels']['maxAbsoluteError'],0)
        self.assertEqual(result['pixelsWithinTolerance']['exactPercent'],100)

    def test_known_channel_metrics(self):
        result=comparison.compare(Frame(bytes([0,0,0,0,10,20,30,255]),2,1,8),Frame(bytes([0,0,0,0,12,20,26,255]),2,1,8))
        self.assertEqual(result['channels']['B']['meanAbsoluteError'],1)
        self.assertEqual(result['channels']['R']['maxAbsoluteError'],4)
        self.assertAlmostEqual(result['allChannels']['rmse'],math.sqrt(20/8))
        self.assertEqual(result['pixelsWithinTolerance']['within2BytesPercent'],50)

    def test_displaced_geometry_has_support_difference(self):
        result=comparison.compare(Frame(bytes([90,90,90,255,0,0,0,0]),2,1,8),Frame(bytes([0,0,0,0,90,90,90,255]),2,1,8))
        self.assertEqual(result['coverageMasks']['alphaOrRGB']['xorPixels'],2)
        self.assertEqual(result['coverageMasks']['alphaOrRGB']['actualMinusReferencePixels'],0)
        self.assertEqual(result['coverageMasks']['alphaOrRGB']['intersectionOverUnion'],0)
        self.assertIsNone(result['transferCandidateFitsOnEqualAlphaCommonSupport']['identity']['rmse'])

    def test_alpha_transfer_keeps_geometry_but_changes_bytes(self):
        reference=Frame(bytes([180,150,100,128]),1,1,4)
        actual=Frame(bytes([90,75,50,128]),1,1,4)
        result=comparison.compare(reference,actual)
        self.assertEqual(result['coverageMasks']['alphaOrRGB']['xorPixels'],0)
        self.assertEqual(result['channels']['A']['maxAbsoluteError'],0)
        fits=result['transferCandidateFitsOnEqualAlphaCommonSupport']
        self.assertLess(fits['encodedRGBMultipliedByAlphaAgain']['rmse'],.4)
        self.assertGreater(fits['identity']['rmse'],50)

    def test_additive_zero_alpha_is_not_discarded(self):
        result=comparison.compare(Frame(bytes([120,0,0,0]),1,1,4),Frame(bytes([0,0,0,0]),1,1,4))
        self.assertEqual(result['alphaCoverage']['reference']['positiveRGBAtZeroAlpha'],1)
        self.assertEqual(result['coverageMasks']['alpha']['xorPixels'],0)
        self.assertEqual(result['coverageMasks']['alphaOrRGB']['xorPixels'],1)
        self.assertEqual(result['channels']['B']['maxAbsoluteError'],120)

    def test_no_implicit_resize_or_truncation(self):
        with self.assertRaises(ValueError): Frame(b'\0'*3,1,1,4)
        with self.assertRaises(ValueError): comparison.compare(Frame(b'\0'*4,1,1,4),Frame(b'\0'*8,2,1,8))

    def test_oracle_hash_and_representation_guard(self):
        with tempfile.TemporaryDirectory() as directory:
            root=pathlib.Path(directory);data=b'\0'*4;(root/'raw.bin').write_bytes(data)
            entry={'name':'test','file':'raw.bin','bytes':4,'sha256':hashlib.sha256(data).hexdigest(),
                   'width':1,'height':1,'rowBytes':4,'pixelFormat':'bgra8Unorm_srgb','blackMatteApplied':False,'desktopCapture':False}
            manifest=root/'oracle.json'
            def save(): manifest.write_text(json.dumps({'frames':[entry]}))
            save();self.assertEqual(comparison.read_oracle(manifest,'test')[0].data,data)
            entry['blackMatteApplied']=True;save()
            with self.assertRaises(ValueError): comparison.read_oracle(manifest,'test')
            entry['blackMatteApplied']=False;entry['sha256']='0'*64;save()
            with self.assertRaises(ValueError): comparison.read_oracle(manifest,'test')
            entry['file']='../raw.bin';save()
            with self.assertRaises(ValueError): comparison.read_oracle(manifest,'test')


if __name__=='__main__': unittest.main()
