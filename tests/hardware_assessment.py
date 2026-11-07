"""Failure-path and numerical policy checks for local hardware evidence."""
import copy
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('assess_scene', ROOT / 'tools/validation/assess_scene.py')
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class Assessment(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.folder = Path(self.temporary.name)
        self.hardware = {
            'format': 'lykoware.hardware-evidence', 'schema_version': 2,
            'commit': 'a' * 40, 'source_dirty': False, 'executable_sha256': 'b' * 64,
            'fixture': {'width': 2560, 'height': 1440, 'ticks': 120, 'warmup': 120,
                        'frames': 100, 'scene_sha256': 'c' * 64, 'commands_sha256': 'd' * 64},
            'nvidia_gpus': [{'name': 'NVIDIA Test GPU', 'total_memory_mib': 12288, 'driver_version': '123.4'}]}
        self.scene = {
            'format': 'lykoware.measurement', 'schema_version': 1, 'hardware_gpu': True,
            'width': 2560, 'height': 1440, 'scene_ticks': 120, 'warmup_draws': 120,
            'frame_times_ms': list(range(1, 101)), 'peak_process_resident_bytes': 4096,
            'replay_state_hash': 123, 'final_state_hash': 456, 'final_tick': 220,
            'device': {'name': 'NVIDIA Test GPU', 'vendor_id': 4318, 'device_id': 999,
                       'driver_version': 123, 'type': 'discrete'}}
        self.write('hardware.json', self.hardware)
        for i in range(1, 4):
            self.write(f'scene-{i}.json', self.scene)

    def write(self, name, data):
        (self.folder / name).write_text(json.dumps(data), encoding='utf-8')

    def budget(self):
        return {'format': 'lykoware.offscreen-budget', 'schema_version': 1, 'status': 'provisional',
                'fixture': copy.deepcopy(self.hardware['fixture']),
                'limits': {'mean_ms': 50.5, 'p99_ms': 99, 'max_ms': 100, 'peak_process_resident_bytes': 4096}}

    def test_recomputes_distribution_and_never_accepts_phase(self):
        # Deliberately misleading precomputed summary must not hide a hitch.
        self.scene['simulation_and_draw'] = {'p99_ms': 0}
        self.write('scene-2.json', self.scene)
        result = MODULE.assess(self.folder)
        self.assertEqual(result['status'], 'collected')
        self.assertEqual(result['runs'][1]['p99_ms'], 99)
        self.assertEqual(result['runs'][1]['mean_ms'], 50.5)
        self.assertFalse(result['phase1_accepted'])
        self.assertIsNone(result['budget_passed'])

    def test_exact_budget_boundary_and_independent_overruns(self):
        policy = self.budget()
        self.assertTrue(MODULE.assess(self.folder, policy)['budget_passed'])
        for key in policy['limits']:
            with self.subTest(key=key):
                changed = copy.deepcopy(policy)
                changed['limits'][key] -= .1
                result = MODULE.assess(self.folder, changed)
                self.assertEqual(result['status'], 'failed')
                self.assertFalse(result['budget_passed'])
                self.assertFalse(result['phase1_accepted'])

    def test_missing_or_ambiguous_inventory_is_incomplete(self):
        for inventory in ([], self.hardware['nvidia_gpus'] * 2,
                          [{'name': 'Different GPU', 'total_memory_mib': 8192, 'driver_version': '1'}]):
            self.hardware['nvidia_gpus'] = inventory
            self.write('hardware.json', self.hardware)
            result = MODULE.assess(self.folder, self.budget())
            self.assertEqual(result['status'], 'incomplete')
            self.assertFalse(result['inventory_matches_selected_device'])

    def test_rejects_software_and_inconsistent_runs(self):
        for key, value in [('hardware_gpu', False), ('width', 1920), ('frame_times_ms', [1]),
                           ('final_tick', 219), ('final_state_hash', 999), ('replay_state_hash', 999),
                           ('device', dict(self.scene['device'], device_id=5))]:
            with self.subTest(key=key):
                changed = dict(self.scene, **{key: value})
                self.write('scene-3.json', changed)
                self.assertEqual(MODULE.assess(self.folder)['status'], 'failed')
        self.write('scene-3.json', self.scene)

    def test_rejects_nonfinite_negative_or_non_numeric_samples(self):
        for value in (float('nan'), float('inf'), -1, True, '1', None):
            self.scene['frame_times_ms'][0] = value
            self.write('scene-1.json', self.scene)
            self.assertEqual(MODULE.assess(self.folder)['status'], 'failed')

    def test_rejects_missing_malformed_and_wrong_shapes(self):
        for name in ('hardware.json', 'scene-1.json'):
            for contents in ('{broken', '[]', 'null'):
                (self.folder / name).write_text(contents, encoding='utf-8')
                self.assertEqual(MODULE.assess(self.folder)['status'], 'failed')
            (self.folder / name).unlink()
            self.assertEqual(MODULE.assess(self.folder)['status'], 'failed')
            self.write(name, self.hardware if name == 'hardware.json' else self.scene)

    def test_rejects_unbound_or_invalid_budget(self):
        for key, value in [('limits', {}), ('limits', {'mean_ms': float('nan')}),
                           ('limits', {'unknown': 2}), ('fixture', {}), ('status', 'accepted')]:
            policy = self.budget()
            policy[key] = value
            self.assertEqual(MODULE.assess(self.folder, policy)['status'], 'failed')

    def test_cli_failure_saves_report_and_success_exits_zero(self):
        command = [sys.executable, str(ROOT / 'tools/validation/assess_scene.py'), str(self.folder)]
        self.assertEqual(subprocess.run(command, capture_output=True).returncode, 0)
        (self.folder / 'scene-2.json').unlink()
        self.assertEqual(subprocess.run(command, capture_output=True).returncode, 1)
        report = json.loads((self.folder / 'assessment.json').read_text())
        self.assertEqual(report['status'], 'failed')
        self.assertTrue(report['errors'])
        self.assertFalse(report['phase1_accepted'])

    def test_bad_budget_replaces_prior_success_report(self):
        command = [sys.executable, str(ROOT / 'tools/validation/assess_scene.py'), str(self.folder)]
        self.assertEqual(subprocess.run(command, capture_output=True).returncode, 0)
        for text in ('null', '[]', '{bad'):
            policy = self.folder / 'policy.json'
            policy.write_text(text, encoding='utf-8')
            self.assertEqual(subprocess.run(command + ['--budget', str(policy)], capture_output=True).returncode, 1)
            result = json.loads((self.folder / 'assessment.json').read_text())
            self.assertEqual(result['status'], 'failed')
            self.assertFalse(result['phase1_accepted'])


if __name__ == '__main__':
    unittest.main()
