#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Diagnostic port of reference KwsGraph/per-hyp keyword rules, not plain ASR.

Preserves the reference's continuation +5 heuristic and its unused threshold
for comparison. These are explicitly reported, NOT promoted to product policy.
"""
import math


class Keywords:
    def __init__(self, text, vocabulary):
        self.nodes = [{"next": {}, "level": 0}]
        self.phrases = []
        for line in text.splitlines():
            if not line.strip() or line.startswith("#"):
                continue
            phones, phrase = line.split("@", 1)
            phones, threshold = phones.split("#", 1)
            phones, boost = phones.split(":", 1)
            ids = [vocabulary[token] for token in phones.split()]
            node = 0
            for token in ids:
                if token not in self.nodes[node]["next"]:
                    self.nodes[node]["next"][token] = len(self.nodes)
                    self.nodes.append({"next": {}, "level": self.nodes[node]["level"] + 1,
                                       "boost": float(boost)})
                node = self.nodes[node]["next"][token]
            self.nodes[node].update(phrase=phrase.strip(), threshold=float(threshold))
            self.phrases.append(phrase.strip())
        if not self.phrases:
            raise ValueError("Empty keyword graph")
        self.reset()

    def reset(self):
        self.node, self.context, self.probs = 0, [0, 0], []

    def consume(self, logits, frame):
        peak = max(logits)
        normalizer = sum(math.exp(value - peak) for value in logits)
        probs = [math.exp(value - peak) / normalizer for value in logits]
        logp = [math.log(value) if value > 1e-12 else -30.0 for value in probs]
        continuations = self.nodes[self.node]["next"]
        candidates = []
        best_next = max(continuations, key=lambda token: logp[token]) if continuations else None
        for token, node in continuations.items():
            if token in (0, 2):
                continue
            score = logp[token] + self.nodes[node]["boost"]
            # Exact legacy reference heuristic, not evidence of acoustic accuracy.
            if token == best_next and self.nodes[self.node]["level"] >= 3 and logp[token] > -9:
                score += 5
            candidates.append((score, token, node))
        chosen = 0
        if candidates:
            score, token, node = max(candidates, key=lambda item: item[0])
            if score >= logp[0] and (self.node > 0 or probs[token] > 0.08):
                chosen = token
                self.context = [self.context[1], token]
                self.node = node
                self.probs.append(probs[token])
        hit = None
        current = self.nodes[self.node]
        if "phrase" in current:
            used = min(len(self.probs), current["level"])
            if sum(value > 0.12 for value in self.probs) >= 2 and used >= 4:
                hit = {"phrase": current["phrase"], "frame": frame,
                       "mean_phone_probability": sum(self.probs[-used:]) / used,
                       "legacy_threshold_ignored": current["threshold"], "continuation_boost": 5}
                self.reset()
        return chosen, hit
