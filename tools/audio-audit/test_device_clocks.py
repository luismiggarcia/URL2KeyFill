import unittest
from analyze_device_clocks import analyse


def fixture(ppm=1):
    rows=[]
    for i in range(301):
        host=10_000_000_000+i*48000;device=round(i*48000*(1+ppm*1e-6))
        rows.append(f'SYNC_CLOCK queue_before={host} queue_after={host} consumed={device} stream={device} hardware_before={host} hardware_after={host} hardware={device+1000000} hardware_valid=1 ratio={1/(1+ppm*1e-6):.12f} phase_error=0')
    rows.append('SYNC_TRACE kind=clock stored=301 omitted=0')
    return '\n'.join(rows)


class DeviceClockTests(unittest.TestCase):
    def test_independent_clocks_and_inverse_rate(self):
        for ppm in [-1,1,20]:
            d=analyse(fixture(ppm))
            self.assertAlmostEqual(d['hardware']['device_per_host_ppm'],ppm,delta=.1)
            self.assertAlmostEqual(d['consumed_queue']['host_per_device_ppm'],-ppm,delta=.1)
            self.assertEqual(d['rejected_hardware_observations'],0)

    def test_unavailable_clock_cannot_be_certified(self):
        with self.assertRaisesRegex(ValueError,'60 seconds'):
            analyse(fixture().replace('hardware_valid=1','hardware_valid=0'))

    def test_truncated_clock_rejected(self):
        with self.assertRaisesRegex(ValueError,'truncated'):
            analyse(fixture().replace('omitted=0','omitted=1'))

    def test_slow_hardware_queries_are_excluded(self):
        log=fixture();log=log.replace('hardware_after=10004800000','hardware_after=10004800096')
        self.assertEqual(analyse(log)['rejected_hardware_observations'],1)


if __name__=='__main__':unittest.main()
