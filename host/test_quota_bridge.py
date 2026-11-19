import unittest
from quota_bridge import normalize_buckets, normalize_window, credit_text


class QuotaNormalizationTest(unittest.TestCase):
    def test_missing_window_is_unavailable(self):
        self.assertIsNone(normalize_window(None))
        self.assertIsNone(normalize_window({"usedPercent": 0}))

    def test_zero_usage_is_real_zero(self):
        window = normalize_window({"usedPercent": 0, "windowDurationMins": 300, "resetsAt": 1791981530})
        self.assertEqual(window["used"], 0)
        self.assertEqual(window["minutes"], 300)
        self.assertEqual(window["reset_local"], "10/14 20:38 UTC+8")

    def test_nonfinite_or_wrong_types_are_unavailable(self):
        for used in (float("nan"), float("inf"), True, "37", None):
            self.assertIsNone(normalize_window(
                {"usedPercent": used, "windowDurationMins": 10080, "resetsAt": 1791981530}))

    def test_percentage_is_clamped(self):
        for used, expected in ((-10, 0), (130, 100)):
            self.assertEqual(normalize_window(
                {"usedPercent": used, "windowDurationMins": 10080, "resetsAt": 1791981530})["used"], expected)

    def test_map_takes_precedence_and_supports_multiple_buckets(self):
        result = normalize_buckets({
            "rateLimits": {"primary": {"usedPercent": 99}},
            "rateLimitsByLimitId": {
                "codex": {"planType": "pro", "primary": {
                    "usedPercent": 38, "windowDurationMins": 10080, "resetsAt": 1791981530}},
                "other": {"primary": None, "secondary": None}},
            "rateLimitResetCredits": {"availableCount": 3}})
        self.assertEqual(list(result), ["codex", "other"])
        self.assertEqual(result["codex"]["primary"]["used"], 38)
        self.assertIsNone(result["codex"]["secondary"])
        self.assertIsNone(result["other"]["primary"])
        self.assertEqual(result["codex"]["reset_credits"], 3)

    def test_legacy_and_missing_snapshot(self):
        self.assertEqual(normalize_buckets({"rateLimits": {"limitId": "old"}})["old"]["bucket"], "old")
        empty = normalize_buckets({})["codex"]
        self.assertIsNone(empty["primary"])
        self.assertIsNone(empty["reset_credits"])
        self.assertEqual(empty["credits"], "--")

    def test_credit_balance_is_never_inferred_as_dollars(self):
        self.assertEqual(credit_text({"balance": "62499.3958333334"}), "62,499.4")
        self.assertEqual(credit_text({"unlimited": True}), "Unlimited")
        self.assertEqual(credit_text({}), "--")
        self.assertEqual(credit_text({"balance": "NaN"}), "--")


if __name__ == "__main__":
    unittest.main()
