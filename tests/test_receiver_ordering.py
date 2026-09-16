"""政策入口与实验隔离检查，不重复执行地形算法。"""

import json
import tempfile
import unittest
from pathlib import Path
from jsonschema.exceptions import ValidationError

from experiment_infrastructure.catalog import ROOT, resolve_case
from experiment_infrastructure.suite import case_variants


class ReceiverOrderingTests(unittest.TestCase):
    def setUp(self):
        self.base = json.loads((ROOT / "configs/experiments/formal/fer_02/peking547-b50000-transactional-t8.json").read_text())

    def resolve(self, value):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "case.json"
            path.write_text(json.dumps(value))
            return resolve_case(path)

    def test_default_explicit_and_invalid(self):
        self.assertEqual(self.resolve(self.base)["receiverOrder"], "composite")
        for policy in ("composite", "error-first"):
            self.assertEqual(self.resolve({**self.base, "receiverOrder": policy})["receiverOrder"], policy)
        with self.assertRaises(ValidationError):
            self.resolve({**self.base, "receiverOrder": "typo"})
        with self.assertRaises(ValueError):
            self.resolve({**self.base, "algorithm": "dod", "heightPolicy": "fit", "flipRecovery": False,
                          "receiverOrder": "error-first"})

    def test_suite_isolates_policy(self):
        source = {**self.base, "receiverOrder": "error-first", "boundaryRefinement": True}
        cases = case_variants(source, [50000], [8], ["classic", "dod", "transactional", "cbt"],
                              ["scaled"], [16], [262144])
        for case in cases:
            expected = "error-first" if case["algorithm"] == "transactional" else "composite"
            self.assertEqual(case["receiverOrder"], expected)
            self.assertEqual(case["boundaryRefinement"], case["algorithm"] == "transactional")


if __name__ == "__main__":
    unittest.main()
