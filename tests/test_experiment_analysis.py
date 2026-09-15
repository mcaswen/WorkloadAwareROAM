"""只测试证据破坏、空值与观察单位，避免重跑算法矩阵。"""
import tempfile
import unittest
from pathlib import Path
from experiment_infrastructure.result_adapters import rows,number,verify
from experiment_infrastructure.analysis import phase
from experiment_infrastructure.catalog import content_hash
from experiment_infrastructure.runner import save

class EvidenceTests(unittest.TestCase):
    def test_truncated_csv(self):
        with tempfile.TemporaryDirectory() as t:
            p=Path(t)/"data.csv";p.write_text("a,b\n1\n")
            with self.assertRaises(ValueError): rows(p,["a","b"],True)

    def test_duplicate_header(self):
        with tempfile.TemporaryDirectory() as t:
            p=Path(t)/"data.csv";p.write_text("a,a\n1,2\n")
            with self.assertRaises(ValueError): rows(p,["a"])

    def test_null_is_not_zero(self):
        self.assertIsNone(number(""));self.assertEqual(number("0"),0)
        with self.assertRaises(ValueError): number("nan")

    def test_tamper_rejected(self):
        with tempfile.TemporaryDirectory() as t:
            root=Path(t);p=root/"frames.csv";p.write_text("original")
            save(root/"manifest.json",{"schemaVersion":"eip-run-v1",
                "artifacts":{"frames.csv":{"sha256":content_hash(p)}}})
            verify(root);p.write_text("changed")
            with self.assertRaises(ValueError): verify(root)

    def test_recovery_not_independent_repeat(self):
        frames=[dict(event="warmup",warmup=1,poseHash="a"),
                dict(event="return",warmup=0,poseHash="a"),
                dict(event="recovery",warmup=0,poseHash="a")]
        self.assertEqual(phase(frames,0),"cold")
        self.assertEqual(phase(frames,1),"return-recovery")
        self.assertEqual(phase(frames,2),"return-recovery")

if __name__=="__main__": unittest.main()
