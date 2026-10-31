import importlib.util
from pathlib import Path
import unittest
spec=importlib.util.spec_from_file_location('endurance',Path(__file__).with_name('run-endurance.py'))
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)


class EnduranceTests(unittest.TestCase):
    def rows(self):
        base=dict.fromkeys(m.HEALTH,'0')
        return [dict(base,running='1',on_air='1',elapsed_s=str(i),scheduled_activity='1',audio_drift_ready='1',audio_rate_ppm='0.5',audio_phase_error_ms='0.1',headroom_min_ms='30') for i in range(120)]

    def capture(self, offset=0):
        return {'channels':{'audio_L':{'median_ms':offset}}}

    def test_complete(self):
        self.assertEqual(m.summarize(self.rows(),self.capture(),self.capture(1),'1080p25',120)['health']['missing_total'],0)

    def test_constant_large_offset_not_accepted(self):
        result=m.summarize(self.rows(),self.capture(-55),self.capture(-55),'1080p25',120)
        self.assertFalse(result['av_quality_passed'])
        self.assertEqual(result['health']['missing_total'],0)

    def test_gap_rejected(self):
        rows=self.rows();rows[50]['missing_total']='1'
        with self.assertRaisesRegex(ValueError,'Continuity'):m.summarize(rows,self.capture(),self.capture(),'1080p25',120)

    def test_short_rejected(self):
        with self.assertRaisesRegex(ValueError,'duration'):m.summarize(self.rows()[:30],self.capture(),self.capture(),'1080p25',120)

    def test_drift_change_rejected(self):
        with self.assertRaisesRegex(ValueError,'one output frame'):m.summarize(self.rows(),self.capture(),self.capture(25),'1080p50',120)

    def test_stalled_source_rejected(self):
        sample=dict(running='1',on_air='1',source_flash_count='10',source_pulse_count='10')
        with self.assertRaisesRegex(RuntimeError,'stopped advancing'):m.validate_source_flow(sample,sample)
        m.validate_source_flow(sample,dict(sample,source_flash_count='20',source_pulse_count='20'))

    def test_continuity_watchdog(self):
        sample=dict(running='1',on_air='1',source_flash_count='10',source_pulse_count='10',missing_total='1')
        with self.assertRaisesRegex(RuntimeError,'Continuity watchdog'):m.validate_source_flow(None,sample)
        sample.update(missing_total='0',audio_drift_ready='0')
        with self.assertRaisesRegex(RuntimeError,'readiness'):m.validate_source_flow(None,sample)

    def test_incremental_partial_rows(self):
        import tempfile
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/'trace.csv';tail=m.TelemetryTail()
            path.write_text('running,on_air,source_flash_count,source_pulse_count\n1,1,1')
            self.assertIsNone(tail.latest(path))
            with path.open('a') as f:f.write(',2\n')
            self.assertEqual(tail.latest(path)['source_pulse_count'],'2')
            self.assertEqual(tail.latest(path)['source_flash_count'],'1')

    def test_own_assertions_required(self):
        kinds=['PreventUserIdleSystemSleep','PreventUserIdleDisplaySleep','PreventSystemSleep']
        assertions='\n'.join('pid 42(caffeinate) '+k for k in kinds)
        m.validate_power("Now drawing from 'AC Power'",assertions,42)
        with self.assertRaisesRegex(RuntimeError,'absent'):m.validate_power("'AC Power'",assertions,43)
        with self.assertRaisesRegex(RuntimeError,'AC power'):m.validate_power("'Battery Power'",assertions,42)


if __name__=='__main__':unittest.main()
