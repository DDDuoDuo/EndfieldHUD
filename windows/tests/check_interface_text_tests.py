#!/usr/bin/env python3
"""Contracts of tools/check_interface_text.py on synthetic sources, then the check of the real windows/ tree."""
import importlib.util
import pathlib
import sys
import tempfile
import unittest

sys.dont_write_bytecode = True
WINDOWS = pathlib.Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("check_interface_text", WINDOWS / "tools/check_interface_text.py")
checker = importlib.util.module_from_spec(spec)
spec.loader.exec_module(checker)


class RuleTests(unittest.TestCase):
    def test_final_full_stop(self):
        for text in ("Done.", "完成。", "完了．"):
            self.assertTrue(checker.ends_with_full_stop(text), text)
        for text in ("Wait...", "Wait…", ".", "v1.3", "Done", ""):
            self.assertFalse(checker.ends_with_full_stop(text), text)

    def test_dot_separator(self):
        for text in ("A · B", "A•", "‧", "･", "画像・動画", "レム・", "・レ", "レ・・レ"):
            self.assertTrue(checker.has_dot_separator(text), text)
        for text in ("レム・ビリトン", "A / B", "画像/動画"):
            self.assertFalse(checker.has_dot_separator(text), text)


class ScanTests(unittest.TestCase):
    def pairs(self, source):
        return [(english, simplified) for english, simplified, _ in checker.call_pairs(source)]

    def test_lookup_calls_are_found(self):
        source = '\n'.join([
            'auto a = core::localized("Retry", "重试", language);',
            'auto b = text("Zoom in", "放大");',
            'toast(d::Icon::warn, "Storage unavailable", "存储不可用", detail);',
            'auto c = tr(*env_, "Brush", "画笔");',
            'auto d = t("Sign-in could not finish. "',
            '           "Please try again", "登录未完成，请重试");',
        ])
        self.assertEqual(self.pairs(source), [("Retry", "重试"), ("Zoom in", "放大"), ("Storage unavailable", "存储不可用"),
                                              ("Brush", "画笔"), ("Sign-in could not finish. Please try again", "登录未完成，请重试")])

    def test_other_literals_are_not_lookups(self):
        source = '\n'.join([
            '// core::localized("Commented out.", "注释。")',
            'return text(l, "Applications", "应用程序", "應用程式", "アプリ", "앱");',
            'set("key", "value");',
            'auto raw = R"(text("A.", "甲。"))";',
        ])
        self.assertEqual(self.pairs(source), [])

    def test_failures_are_reported(self):
        with tempfile.TemporaryDirectory(prefix="endfield-interface-text-test-") as temporary:
            windows = pathlib.Path(temporary)
            (windows / "core").mkdir()
            (windows / "core/localization_catalog.inc").write_text(
                '// Generated\n    {"Retry","重试","重試","再試行","다시 시도"},\n', encoding="utf-8")
            (windows / "modules").mkdir()
            (windows / "modules/a.cpp").write_text('\n'.join([
                'auto ok = core::localized("Retry", "重试", l);',
                'auto stop = core::localized("Retry.", "重试。", l);',
                'auto dot = core::localized("A · B", "甲 · 乙", l);',
            ]), encoding="utf-8")
            (windows / "modules/tests").mkdir()
            (windows / "modules/tests/b.cpp").write_text('auto skipped = core::localized("Skipped.", "跳过。", l);', encoding="utf-8")
            failures, checked = checker.check(windows)
        self.assertEqual(checked, 3)
        self.assertEqual(sorted(failure.split(":")[0] for failure in failures),
                         ["Interface text ends in a full stop", "Interface text has a middle dot separator", "Pair missing from the catalog", "Pair missing from the catalog"])


class TreeTests(unittest.TestCase):
    def test_windows_interface_text(self):
        failures, checked = checker.check(WINDOWS)
        self.assertEqual(failures, [])
        self.assertGreater(checked, checker.MINIMUM_PAIRS)


if __name__ == "__main__":
    unittest.main()
