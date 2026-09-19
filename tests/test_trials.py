"""Conversion failures must leave previously generated trials intact."""
import argparse
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import import_trial
import refine_trials
from trial_io import publish_directories


class TrialTests(unittest.TestCase):
    def test_absolute_package_relationship(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'test.pptx'
            with zipfile.ZipFile(path, 'w') as archive:
                archive.writestr('ppt/slides/_rels/slide1.xml.rels',
                    '<Relationships><Relationship Id="r1" Target="/ppt/media/image.png"/>'
                    '<Relationship Id="r2" Target="../media/other.png"/></Relationships>')
            with zipfile.ZipFile(path) as archive:
                self.assertEqual(import_trial.relationships(archive, 'ppt/slides/slide1.xml'),
                                 {'r1': 'ppt/media/image.png', 'r2': 'ppt/media/other.png'})

    def test_literal_markdown_and_repeated_labels(self):
        self.assertEqual(import_trial.escape('# Heading'), r'\# Heading')
        self.assertEqual(import_trial.escape('1. Point'), r'1\. Point')
        labels = [{'text': 'Repeated', 'box': (x, 0, 100, 100)} for x in (0, 200)]
        self.assertEqual(import_trial.markdown_text(labels, '2025', 1).count('Repeated'), 2)

    def test_failed_import_preserves_previous_files(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / 'trial'
            output.mkdir()
            (output / 'presentation.md').write_text('original')
            def fail(args):
                Path(args.output).mkdir()
                (Path(args.output) / 'presentation.md').write_text('partial')
                raise ValueError('missing picture')
            args = argparse.Namespace(output=str(output), overwrite=True)
            with patch.object(import_trial, 'convert', side_effect=fail):
                with self.assertRaises(ValueError):
                    import_trial.run(args)
            self.assertEqual((output / 'presentation.md').read_text(), 'original')
            self.assertEqual(list(Path(directory).iterdir()), [output])

    def test_failed_refinement_preserves_all_three_trials(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / 'trials'
            for year in (2023, 2024, 2025):
                deck = root / f'rails-world-{year}'
                deck.mkdir(parents=True)
                (deck / 'presentation.md').write_text(str(year))
            def fail(staging, *_):
                (staging / 'rails-world-2023/presentation.md').write_text('partial')
                raise ValueError('missing code')
            with patch.object(refine_trials, 'refine', side_effect=fail):
                with self.assertRaises(ValueError):
                    refine_trials.run(root, None, Path(directory))
            for year in (2023, 2024, 2025):
                self.assertEqual((root / f'rails-world-{year}/presentation.md').read_text(), str(year))

    def test_publication_failure_rolls_back_prior_directories(self):
        with tempfile.TemporaryDirectory() as directory:
            root, staging = Path(directory) / 'trials', Path(directory) / 'staged'
            for parent, content in ((root, 'old'), (staging, 'new')):
                for name in ('a', 'b'):
                    (parent / name).mkdir(parents=True)
                    (parent / name / 'presentation.md').write_text(content)
            rename = os.replace
            def fail(source, target):
                if Path(source) == staging / 'b':
                    raise OSError('publication failed')
                rename(source, target)
            with patch('trial_io.os.replace', side_effect=fail):
                with self.assertRaises(OSError):
                    publish_directories(staging, root, ['a', 'b'])
            for name in ('a', 'b'):
                self.assertEqual((root / name / 'presentation.md').read_text(), 'old')
                self.assertEqual((staging / name / 'presentation.md').read_text(), 'new')

    def test_failed_rollback_keeps_originals_for_recovery(self):
        with tempfile.TemporaryDirectory() as directory:
            root, staging = Path(directory) / 'trials', Path(directory) / 'staged'
            for parent, content in ((root, 'old'), (staging, 'new')):
                for name in ('a', 'b'):
                    (parent / name).mkdir(parents=True)
                    (parent / name / 'presentation.md').write_text(content)
            rename = os.replace
            def fail(source, target):
                if Path(source) == staging / 'b' or Path(source).parent.name.startswith('.hype-previous-'):
                    raise OSError('destination unavailable')
                rename(source, target)
            with patch('trial_io.os.replace', side_effect=fail):
                with self.assertRaisesRegex(RuntimeError, 'originals remain'):
                    publish_directories(staging, root, ['a', 'b'])
            previous = next(root.glob('.hype-previous-*'))
            for name in ('a', 'b'):
                self.assertEqual((previous / name / 'presentation.md').read_text(), 'old')


if __name__ == '__main__':
    unittest.main()
