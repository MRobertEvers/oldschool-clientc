#!/usr/bin/env python3
"""Strict positive/negative controls for the explicit hidden-chat oracle."""
import contextlib
import io
import unittest
from gameframe_pixels import check_osrs_chat_hidden


def node(index, parent, component, paint=0, input_=0):
    return f"NATIVE_UI node={index} incarnation=1 parent={parent} com={component} type=rs_layer hidden=0 native_paint={paint} native_input={input_} native_hide=0\n"


class HiddenChat(unittest.TestCase):
    def setUp(self):
        self.log = node(1, -1, 162 << 16)
        for i, child in enumerate((5, 8, 12, 16, 20, 24, 28), 2):
            self.log += node(i, 1, (162 << 16) | child)
        self.log += node(20, 2, -1)

    def failures(self, log):
        result = []
        with contextlib.redirect_stdout(io.StringIO()):
            check_osrs_chat_hidden(log, 601, result)
        return result

    def test_hidden_controls_remain_present(self):
        self.assertEqual(self.failures(self.log), [])

    def test_missing_control_is_not_hidden_success(self):
        self.assertIn("osrs_hidden_chat_controls", self.failures(self.log.replace(node(2, 1, (162 << 16) | 5), "")))

    def test_visible_native_control_fails(self):
        self.assertIn("osrs_chat_subtree_hidden", self.failures(self.log.replace(node(2, 1, (162 << 16) | 5), node(2, 1, (162 << 16) | 5, 1, 1))))

    def test_dynamic_descendant_cannot_keep_input_or_paint(self):
        for paint, input_ in ((1, 0), (0, 1)):
            self.assertIn("osrs_chat_subtree_hidden", self.failures(self.log.replace(node(20, 2, -1), node(20, 2, -1, paint, input_))))


if __name__ == "__main__":
    unittest.main()
