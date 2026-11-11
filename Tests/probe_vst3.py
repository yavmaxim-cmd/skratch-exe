"""Minimal Windows x64 VST3 ABI smoke test. Uses ctypes, no third-party modules."""
import ctypes as c
import json
import math
import sys
import uuid

def method(obj, index, result, *args):
    table = c.cast(obj, c.POINTER(c.POINTER(c.c_void_p))).contents
    return c.WINFUNCTYPE(result, c.c_void_p, *args)(table[index])

def ok(result, name):
    if result != 0:
        raise RuntimeError(f"{name}: {result}")
    print("PASS", name)

class ClassInfo(c.Structure):
    _fields_ = [("cid", c.c_char * 16), ("cardinality", c.c_int32),
                ("category", c.c_char * 32), ("name", c.c_char * 64)]

class Setup(c.Structure):
    _fields_ = [("mode", c.c_int32), ("size", c.c_int32),
                ("maxSamples", c.c_int32), ("sampleRate", c.c_double)]

class Bus(c.Structure):
    _fields_ = [("channels", c.c_int32), ("silence", c.c_uint64),
                ("buffers", c.POINTER(c.POINTER(c.c_float)))]

class Process(c.Structure):
    _fields_ = [("mode", c.c_int32), ("size", c.c_int32), ("samples", c.c_int32),
                ("numInputs", c.c_int32), ("numOutputs", c.c_int32),
                ("inputs", c.POINTER(Bus)), ("outputs", c.POINTER(Bus)),
                ("inputParams", c.c_void_p), ("outputParams", c.c_void_p),
                ("inputEvents", c.c_void_p), ("outputEvents", c.c_void_p),
                ("context", c.c_void_p)]

class NoteOn(c.Structure):
    _fields_ = [("channel", c.c_int16), ("pitch", c.c_int16), ("tuning", c.c_float),
                ("velocity", c.c_float), ("length", c.c_int32), ("noteId", c.c_int32)]

class Event(c.Structure):
    _fields_ = [("bus", c.c_int32), ("offset", c.c_int32), ("ppq", c.c_double),
                ("flags", c.c_uint16), ("type", c.c_uint16), ("alignment", c.c_uint32),
                ("note", NoteOn), ("padding", c.c_uint64)]

module = c.WinDLL(sys.argv[1])
module.InitDll.restype = c.c_bool
assert module.InitDll(), "InitDll"
module.GetPluginFactory.restype = c.c_void_p
factory = c.c_void_p(module.GetPluginFactory())
assert factory.value, "GetPluginFactory"
count = method(factory, 4, c.c_int32)(factory)
assert count > 0, "class count"
component = c.c_void_p()
instrument = len(sys.argv) > 2 and sys.argv[2] == "instrument"
for n in range(count):
    info = ClassInfo()
    ok(method(factory, 5, c.c_int32, c.c_int32, c.POINTER(ClassInfo))(factory, n, c.byref(info)), "class metadata")
    print(json.dumps({"name": info.name.decode(), "category": info.category.decode()}))
    if info.category == b"Audio Module Class":
        cid = c.string_at(c.addressof(info), 16)
        print("Component CID:", uuid.UUID(bytes_le=cid).hex.upper())
        iid = uuid.UUID("E831FF31-F2D5-4301-928E-BBEE25697802").bytes_le
        ok(method(factory, 6, c.c_int32, c.c_char_p, c.c_char_p, c.POINTER(c.c_void_p))(factory, cid, iid, c.byref(component)), "create IComponent")
assert component.value, "audio component"
ok(method(component, 3, c.c_int32, c.c_void_p)(component, None), "component initialize")
bus_count = method(component, 7, c.c_int32, c.c_int32, c.c_int32)
assert bus_count(component, 0, 0) == (0 if instrument else 1)
assert bus_count(component, 0, 1) == 1
assert bus_count(component, 1, 0) == 1
print("PASS audio and event bus counts")
audio = c.c_void_p()
iid = uuid.UUID("42043F99-B7DA-453C-A569-E79D9AAEC33D").bytes_le
ok(method(component, 0, c.c_int32, c.c_char_p, c.POINTER(c.c_void_p))(component, iid, c.byref(audio)), "query IAudioProcessor")
setup = Setup(0, 0, 256, 48000.0)
ok(method(audio, 7, c.c_int32, c.POINTER(Setup))(audio, c.byref(setup)), "setup 48 kHz / 256")
activate_bus = method(component, 10, c.c_int32, c.c_int32, c.c_int32, c.c_int32, c.c_uint8)
for direction in ((1,) if instrument else (0, 1)):
    ok(activate_bus(component, 0, direction, 0, 1), "activate audio bus")
ok(activate_bus(component, 1, 0, 0, 1), "activate MIDI event bus")
ok(method(component, 11, c.c_int32, c.c_uint8)(component, 1), "activate component")
ok(method(audio, 8, c.c_int32, c.c_uint8)(audio, 1), "start processing")
storage = [(c.c_float * 256)() for _ in range(4)]
inputs = (c.POINTER(c.c_float) * 2)(storage[0], storage[1])
outputs = (c.POINTER(c.c_float) * 2)(storage[2], storage[3])
ib, ob = Bus(2, 0, inputs), Bus(2, 0, outputs)
process = Process(0, 0, 256, 0 if instrument else 1, 1,
                  None if instrument else c.pointer(ib), c.pointer(ob), None, None, None, None, None)
assert method(audio, 6, c.c_uint32)(audio) == 0
print("PASS zero PDC samples")
if instrument:
    class EventList(c.Structure):
        _fields_ = [("vtable", c.POINTER(c.c_void_p))]

    # Stab starts with a non-zero target velocity.
    note = Event(0, 109, 0.0, 0, 0, 0, NoteOn(0, 53, 0.0, 0.8, 0, -1), 0)
    count_events = c.WINFUNCTYPE(c.c_int32, c.c_void_p)(lambda _: 1)

    def get_event(_, index, destination):
        if index != 0:
            return 1
        c.memmove(destination, c.byref(note), c.sizeof(Event))
        return 0

    get_events = c.WINFUNCTYPE(c.c_int32, c.c_void_p, c.c_int32, c.POINTER(Event))(get_event)
    vtable = (c.c_void_p * 6)()
    vtable[3] = c.cast(count_events, c.c_void_p).value
    vtable[4] = c.cast(get_events, c.c_void_p).value
    events = EventList(vtable)
    process.inputEvents = c.addressof(events)
    ok(method(audio, 9, c.c_int32, c.POINTER(Process))(audio, c.byref(process)), "instrument note process")
    assert max(map(abs, storage[2][:109])) == 0
    first = next(i for i, value in enumerate(storage[2]) if abs(value) > 1e-10)
    assert 109 <= first <= 113, first
    assert max(map(abs, storage[2][109:])) > 0.001
    print(f"PASS Note On at sample 109, first output at sample {first}")
    process.inputEvents = None
if not instrument and "automation" in sys.argv:
    # VST3 host-owned queues. Values are normalized and delivered via ProcessData.
    class ParameterQueue(c.Structure):
        _fields_ = [("vtable", c.POINTER(c.c_void_p)), ("id", c.c_uint32), ("value", c.c_double)]

    class ParameterChanges(c.Structure):
        _fields_ = [("vtable", c.POINTER(c.c_void_p)), ("count", c.c_int32),
                    ("queues", c.POINTER(ParameterQueue))]

    def vst_id(text):
        result = 0
        for character in text:
            result = (31 * result + ord(character)) & 0xffffffff
        return result & 0x7fffffff

    def point(queue, index, offset, value):
        if index != 0:
            return 1
        offset[0], value[0] = 0, c.cast(queue, c.POINTER(ParameterQueue)).contents.value
        return 0

    def get_queue(changes, index):
        data = c.cast(changes, c.POINTER(ParameterChanges)).contents
        return c.addressof(data.queues[index]) if 0 <= index < data.count else None

    get_id = c.WINFUNCTYPE(c.c_uint32, c.c_void_p)(
        lambda q: c.cast(q, c.POINTER(ParameterQueue)).contents.id)
    get_count = c.WINFUNCTYPE(c.c_int32, c.c_void_p)(lambda _: 1)
    get_point = c.WINFUNCTYPE(c.c_int32, c.c_void_p, c.c_int32,
                             c.POINTER(c.c_int32), c.POINTER(c.c_double))(point)
    queue_table = (c.c_void_p * 7)()
    for index, callback in enumerate((get_id, get_count, get_point), 3):
        queue_table[index] = c.cast(callback, c.c_void_p).value
    changes_count = c.WINFUNCTYPE(c.c_int32, c.c_void_p)(
        lambda changes: c.cast(changes, c.POINTER(ParameterChanges)).contents.count)
    get_changes_queue = c.WINFUNCTYPE(c.c_void_p, c.c_void_p, c.c_int32)(get_queue)
    changes_table = (c.c_void_p * 6)()
    changes_table[3] = c.cast(changes_count, c.c_void_p).value
    changes_table[4] = c.cast(get_changes_queue, c.c_void_p).value
    process_audio = method(audio, 9, c.c_int32, c.POINTER(Process))

    def queue_settings(settings):
        queues = (ParameterQueue * len(settings))(
            *(ParameterQueue(queue_table, vst_id(key), value) for key, value in settings.items()))
        return ParameterChanges(changes_table, len(settings), queues)

    def render_blocks(settings, live_value):
        changes = queue_settings(settings)
        process.inputParams = c.addressof(changes)
        for block in range(64):
            for i in range(256):
                storage[0][i] = live_value
                storage[1][i] = -live_value / 2
            result = process_audio(audio, c.byref(process))
            if result != 0:
                raise RuntimeError(f"automation process returned {result}")
            process.inputParams = None
        return [math.sqrt(sum(value * value for value in channel) / 256) for channel in storage[2:]]

    initial = render_blocks({"source": 1, "gain": 36 / 42, "dj.inputGain": 24 / 42}, .2)
    reduced = render_blocks({"dj.inputGain": 12 / 42}, .2)
    ratios = [b / a for a, b in zip(initial, reduced)]
    assert all(abs(ratio - 10 ** (-12 / 20)) < .001 for ratio in ratios), ratios
    print(f"PASS VST3 host automation Input Gain -12 dB, stereo RMS ratios {ratios}")
    # The demo is a finite periodic source. Compare energy across equal 16,384-frame windows.
    initial_deck = render_blocks({"source": 0, "play": 1, "dj.inputGain": 24 / 42}, 0)
    reduced_deck = render_blocks({"dj.inputGain": 0}, 0)
    assert initial_deck[0] > .015, initial_deck
    print(f"FX File Deck automation RMS before/after: {initial_deck[0]:.7f}, {reduced_deck[0]:.7f}")
    if "baseline" not in sys.argv:
        assert reduced_deck[0] < initial_deck[0] * .18, reduced_deck
        print("PASS VST3 host automation reaches the FX File Deck")

    render_blocks({"source": 1, "play": 0, "dj.inputGain": 24 / 42,
                   "dj.enabled": 1, "dj.echoOn": 1, "dj.echoMix": 1,
                   "dj.echoSync": 0, "dj.echoMs": 40 / 1990}, 0)
    master = queue_settings({"gain": 16 / 42})
    tail = None
    for block in range(10):
        for i in range(256):
            storage[0][i] = .4 if block == 0 and i == 0 else 0
            storage[1][i] = -.2 if block == 0 and i == 0 else 0
        process.inputParams = c.addressof(master) if block == 5 else None
        ok_code = process_audio(audio, c.byref(process))
        assert ok_code == 0, ok_code
        if block == 9:
            tail = storage[2][96]
    process.inputParams = None
    expected_tail = .4 if "baseline" in sys.argv else .04
    print(f"VST3 Master -20 dB after impulse, stored echo peak: {tail:.7f}")
    assert abs(tail - expected_tail) < .002, tail
    print("PASS VST3 stored echo measurement")

for _ in range(64):
    result = method(audio, 9, c.c_int32, c.POINTER(Process))(audio, c.byref(process))
    if result != 0:
        raise RuntimeError(f"process returned {result}")
assert all(math.isfinite(v) for channel in storage[2:] for v in channel)
print("PASS 64 VST3 process blocks, finite output")
method(audio, 8, c.c_int32, c.c_uint8)(audio, 0)
method(component, 11, c.c_int32, c.c_uint8)(component, 0)
method(audio, 2, c.c_uint32)(audio)
ok(method(component, 4, c.c_int32)(component), "component terminate")
method(component, 2, c.c_uint32)(component)
method(factory, 2, c.c_uint32)(factory)
module.ExitDll()
print("VST3 ABI smoke test passed")
