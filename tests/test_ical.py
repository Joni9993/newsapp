"""Runs the actual firmware calendar parser on the host; no live calendar data."""
import ctypes as C
import datetime as D
from pathlib import Path
import unittest

lib=C.CDLL(str(Path('build/ical-test.dll').resolve()))
class Occurrence(C.Structure):
    _fields_=[('start',C.c_int64),('end',C.c_int64),('all_day',C.c_bool),('title',C.c_char*128),('location',C.c_char*96)]
lib.ical_create.restype=C.c_void_p
lib.ical_feed.argtypes=[C.c_void_p,C.c_char_p,C.c_size_t];lib.ical_feed.restype=C.c_bool
lib.ical_finish.argtypes=[C.c_void_p,C.c_int64,C.POINTER(Occurrence),C.POINTER(C.c_int)];lib.ical_finish.restype=C.c_bool
lib.ical_error.argtypes=[C.c_void_p];lib.ical_error.restype=C.c_char_p
lib.ical_free.argtypes=[C.c_void_p]
lib.ical_set_window.argtypes=[C.c_void_p,C.c_int64]
lib.ical_dropped.argtypes=[C.c_void_p];lib.ical_dropped.restype=C.c_int
def epoch(t):return int(D.datetime.fromisoformat(t).replace(tzinfo=D.timezone.utc).timestamp())
def event(uid='one',start='20261018T100000Z',end='20261018T110000Z',extra='',summary='Termin',params=''):
    return f'BEGIN:VEVENT\nUID:{uid}\nDTSTART{params}:{start}\nDTEND{params}:{end}\nSUMMARY:{summary}\n{extra}\nEND:VEVENT\n'
def parse(body,now='2026-10-18T00:00:00',chunk=7,wrap=True,window=False,stats=None):
    raw=(('BEGIN:VCALENDAR\nVERSION:2.0\n'+body+'END:VCALENDAR\n') if wrap else body).replace('\n','\r\n').encode()
    p=lib.ical_create();out=(Occurrence*20)();n=C.c_int();ok=True
    if window:lib.ical_set_window(p,epoch(now))
    try:
        for i in range(0,len(raw),chunk):
            if not lib.ical_feed(p,raw[i:i+chunk],len(raw[i:i+chunk])):ok=False;break
        if ok:ok=lib.ical_finish(p,epoch(now),out,C.byref(n))
        if stats is not None:stats['dropped']=lib.ical_dropped(p)
        return ok,list(out)[:n.value],lib.ical_error(p).decode()
    finally:lib.ical_free(p)
class CalendarTests(unittest.TestCase):
    def test_single_and_chunk_boundaries(self):
        for n in [1,2,7,1024]:
            ok,items,err=parse(event(),chunk=n);self.assertTrue(ok,err);self.assertEqual(len(items),1);self.assertEqual(items[0].start,epoch('2026-10-18T10:00:00'))
    def test_folded_escaped_unicode(self):
        ok,a,e=parse(event(summary='Grüße\\, Welt\\nZweite\n Zeile'));self.assertTrue(ok,e);self.assertEqual(a[0].title.decode(),'Grüße, Welt ZweiteZeile')
    def test_weekly_exclusion_and_move(self):
        base=event(start='20261005T100000Z',end='20261005T110000Z',extra='RRULE:FREQ=WEEKLY;BYDAY=MO,WE\nEXDATE:20261019T100000Z')
        override=event(start='20261022T120000Z',end='20261022T130000Z',extra='RECURRENCE-ID:20261021T100000Z',summary='Verschoben')
        ok,a,e=parse(base+override);self.assertTrue(ok,e);self.assertEqual(len(a),1);self.assertEqual(a[0].start,epoch('2026-10-22T12:00:00'))
    def test_cancelled_instance(self):
        base=event(extra='RRULE:FREQ=DAILY;COUNT=3')
        cancelled='BEGIN:VEVENT\nUID:one\nRECURRENCE-ID:20261019T100000Z\nSTATUS:CANCELLED\nEND:VEVENT\n'
        ok,a,e=parse(base+cancelled);self.assertTrue(ok,e);self.assertEqual(len(a),2)
    def test_all_day(self):
        ok,a,e=parse(event(start='20261018',end='20261020',params=';VALUE=DATE'));self.assertTrue(ok,e);self.assertTrue(a[0].all_day);self.assertEqual(a[0].end-a[0].start,172800)
    def test_dst_end_daily(self):
        ok,a,e=parse(event(start='20261024T090000',end='20261024T100000',params=';TZID=Europe/Berlin',extra='RRULE:FREQ=DAILY;COUNT=3'),now='2026-10-23T00:00:00');self.assertTrue(ok,e);self.assertEqual([x.start for x in a],[epoch('2026-10-24T07:00:00'),epoch('2026-10-25T08:00:00'),epoch('2026-10-26T08:00:00')])
    def test_dst_start_daily(self):
        ok,a,e=parse(event(start='20260328T090000',end='20260328T100000',params=';TZID=Europe/Berlin',extra='RRULE:FREQ=DAILY;COUNT=3'),now='2026-03-27T00:00:00');self.assertTrue(ok,e);self.assertEqual(a[1].start-a[0].start,23*3600)
    def test_monthly_last_weekday(self):
        ok,a,e=parse(event(start='20260925T100000Z',end='20260925T110000Z',extra='RRULE:FREQ=MONTHLY;BYDAY=-1FR'),now='2026-10-25T00:00:00');self.assertTrue(ok,e);self.assertEqual(a[0].start,epoch('2026-10-30T10:00:00'))
    def test_count_until_interval(self):
        ok,a,e=parse(event(extra='RRULE:FREQ=DAILY;INTERVAL=2;COUNT=3;UNTIL=20261020T100000Z'));self.assertTrue(ok,e);self.assertEqual(len(a),2)
    def test_unknown_rule_rejected(self):
        ok,a,e=parse(event(extra='RRULE:FREQ=MONTHLY;BYSETPOS=1'));self.assertFalse(ok);self.assertIn('nicht unterstützt',e)
    def test_unknown_timezone_rejected(self):
        ok,a,e=parse(event(start='20261018T100000',end='20261018T110000',params=';TZID=Planet/Mars'));self.assertFalse(ok);self.assertIn('Zeitzone',e)
    def test_truncated_rejected(self):
        ok,a,e=parse('BEGIN:VCALENDAR\n'+event(),wrap=False);self.assertFalse(ok)
    def test_long_summary_truncated(self):
        ok,a,e=parse(event(summary='a'*3000));self.assertTrue(ok,e);self.assertTrue(a[0].title.decode().startswith('aaaa'))
    def test_long_description_ignored(self):
        ok,a,e=parse(event(extra='DESCRIPTION:'+'x'*5000+'\n '+'y'*3000));self.assertTrue(ok,e);self.assertEqual(len(a),1)
    def test_long_structural_line_rejected(self):
        ok,a,e=parse(event(uid='u'*3000));self.assertFalse(ok);self.assertIn('zu lang',e)
    def test_event_overflow_keeps_earliest(self):
        st={};ok,a,e=parse(''.join(event(uid=str(i),start=f'20261018T{i%24:02}{i//24:02}00Z',end=f'20261018T{i%24:02}{i//24+1:02}00Z') for i in reversed(range(40))),stats=st)
        self.assertTrue(ok,e);self.assertEqual(st['dropped'],8);self.assertEqual(len(a),20);self.assertEqual(a[0].start,epoch('2026-10-18T00:00:00'))
    def test_window_skips_history(self):
        old=''.join(event(uid=f'old{i}',start=f'2025{1+i%12:02}{1+i%28:02}T100000Z',end=f'2025{1+i%12:02}{1+i%28:02}T110000Z') for i in range(500))
        ended=event(uid='ended',start='20200106T090000Z',end='20200106T100000Z',extra='RRULE:FREQ=WEEKLY;UNTIL=20231231T000000Z;BYSETPOS=1')
        weekly=event(uid='weekly',start='20240101T090000Z',end='20240101T100000Z',extra='RRULE:FREQ=WEEKLY'+''.join(f'\nEXDATE:2024{1+i%12:02}{1+i%28:02}T090000Z' for i in range(40)),summary='Jour fixe')
        st={};ok,a,e=parse(old+ended+weekly+event(summary='Heute'),window=True,chunk=1024,stats=st)
        self.assertTrue(ok,e);self.assertEqual(st['dropped'],0);self.assertEqual([x.title.decode() for x in a],['Heute','Jour fixe'])
    def test_result_sorted_bounded(self):
        ok,a,e=parse(''.join(event(uid=str(i),start=f'20261018T{i%24:02}0000Z',end=f'20261018T{i%24:02}3000Z') for i in reversed(range(30))));self.assertTrue(ok,e);self.assertEqual(len(a),20);self.assertEqual([x.start for x in a],sorted(x.start for x in a))
    def test_newer_sequence_wins(self):
        ok,a,e=parse(event(extra='SEQUENCE:1')+event(start='20261019T120000Z',end='20261019T130000Z',extra='SEQUENCE:2'));self.assertTrue(ok,e);self.assertEqual(len(a),1);self.assertEqual(a[0].start,epoch('2026-10-19T12:00:00'))
    def test_deleted_event_disappears(self):
        self.assertEqual(len(parse(event())[1]),1);ok,a,e=parse('');self.assertTrue(ok,e);self.assertEqual(a,[])
    def test_rdate(self):
        ok,a,e=parse(event(extra='RDATE:20261020T100000Z'));self.assertTrue(ok,e);self.assertEqual(len(a),2)
if __name__=='__main__':unittest.main(verbosity=2)
