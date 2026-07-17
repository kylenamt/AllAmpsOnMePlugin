import argparse
import csv
import math
import pickle
import re
import sys
import wave
from pathlib import Path

import numpy as np


def _median_nonzero(row):
	nz = row[row > 0]
	if nz.size == 0:
		return 0.0
	return float(np.median(nz))


def _derive_f0_list(dataset, num_rows):
	if len(dataset) > 4 and isinstance(dataset[4], list) and len(dataset[4]) == num_rows:
		f0 = np.array(dataset[4], dtype=np.float64)
		if np.any(f0 > 0.0):
			return f0

	if len(dataset) > 0 and isinstance(dataset[0], np.ndarray) and dataset[0].shape[0] == num_rows:
		return np.array([_median_nonzero(dataset[0][i]) for i in range(num_rows)], dtype=np.float64)

	raise ValueError("Unable to derive f0 list from dataset.")


def _derive_velocity_list(dataset, num_rows):
	if len(dataset) > 1 and isinstance(dataset[1], np.ndarray):
		if dataset[1].ndim == 2 and dataset[1].shape[0] == num_rows:
			return np.array([_median_nonzero(dataset[1][i]) for i in range(num_rows)], dtype=np.float64)
		if dataset[1].ndim == 1 and dataset[1].shape[0] == num_rows:
			return dataset[1].astype(np.float64)

	raise ValueError("Unable to derive velocity list from dataset.")


def _next_pow2(n):
	if n <= 0:
		return 1
	return 1 << (int(np.ceil(np.log2(n))))


def _analyze_partials(signal, f0, sample_rate, max_partials, start_frac, length_frac,
					  band_ratio, min_band_hz):
	if f0 <= 0.0 or sample_rate <= 0.0:
		return np.zeros(max_partials, dtype=np.float64)

	start = int(len(signal) * start_frac)
	length = int(len(signal) * length_frac)
	if length < 8:
		return np.zeros(max_partials, dtype=np.float64)

	segment = signal[start:start + length].astype(np.float64)
	if segment.size < 8:
		return np.zeros(max_partials, dtype=np.float64)

	segment -= np.mean(segment)
	window = np.hanning(segment.size)
	nfft = _next_pow2(segment.size)
	spectrum = np.fft.rfft(segment * window, nfft)
	magnitude = np.abs(spectrum)
	freqs = np.fft.rfftfreq(nfft, d=1.0 / sample_rate)

	coeffs = np.zeros(max_partials, dtype=np.float64)
	nyquist = sample_rate * 0.5

	for n in range(1, max_partials + 1):
		freq = f0 * n
		if freq >= nyquist:
			break

		band_hz = max(f0 * band_ratio, min_band_hz)
		lo = max(0.0, freq - band_hz)
		hi = min(nyquist, freq + band_hz)
		bin_lo = int(np.searchsorted(freqs, lo))
		bin_hi = int(np.searchsorted(freqs, hi))

		if bin_hi <= bin_lo:
			bin_hi = min(bin_lo + 1, magnitude.size)

		coeffs[n - 1] = float(np.max(magnitude[bin_lo:bin_hi]))

	return coeffs


def _normalize_coeffs(coeffs, mode, min_db):
	if mode == "none":
		return coeffs

	if mode == "fundamental":
		ref = coeffs[0] if coeffs.size > 0 else 0.0
	else:
		ref = float(np.max(coeffs)) if coeffs.size > 0 else 0.0

	if ref > 0.0:
		coeffs = coeffs / ref

	if min_db is not None:
		min_lin = 10.0 ** (min_db / 20.0)
		coeffs[coeffs < min_lin] = 0.0

	return np.clip(coeffs, 0.0, 1.0)


def _freq_to_midi(freq_hz):
	return int(round(69.0 + 12.0 * math.log2(freq_hz / 440.0)))


def _midi_to_freq(midi_note):
	return 440.0 * (2.0 ** ((midi_note - 69.0) / 12.0))


def _load_kmax_partials(script_dir):
	constants_path = script_dir / "AddSynthConstants.h"
	if not constants_path.exists():
		return None

	text = constants_path.read_text(encoding="utf-8")
	match = re.search(r"kMaxPartials\s*=\s*(\d+)", text)
	if not match:
		return None

	return int(match.group(1))


def _read_wav_mono(path):
	with wave.open(str(path), "rb") as wav:
		channels = wav.getnchannels()
		sample_rate = wav.getframerate()
		sample_width = wav.getsampwidth()
		frame_count = wav.getnframes()
		raw = wav.readframes(frame_count)

	if frame_count == 0:
		return sample_rate, np.zeros(0, dtype=np.float32)

	if sample_width == 1:
		data = np.frombuffer(raw, dtype=np.uint8).astype(np.int16) - 128
		denom = 128.0
	elif sample_width == 2:
		data = np.frombuffer(raw, dtype="<i2")
		denom = 32768.0
	elif sample_width == 3:
		bytes_arr = np.frombuffer(raw, dtype=np.uint8)
		bytes_arr = bytes_arr.reshape(-1, 3)
		vals = (bytes_arr[:, 0].astype(np.int32) |
				(bytes_arr[:, 1].astype(np.int32) << 8) |
				(bytes_arr[:, 2].astype(np.int32) << 16))
		sign = vals & 0x800000
		data = vals - (sign << 1)
		denom = float(1 << 23)
	elif sample_width == 4:
		data = np.frombuffer(raw, dtype="<i4")
		denom = float(1 << 31)
	else:
		raise ValueError(f"Unsupported sample width: {sample_width}")

	if channels > 1:
		data = data.reshape(-1, channels).mean(axis=1)

	return sample_rate, (data.astype(np.float32) / denom)


def _sanitize_identifier(text):
	clean = "".join(ch if (ch.isalnum() or ch == "_") else "_" for ch in text)
	if not clean:
		return "Instrument"
	if clean[0].isdigit():
		return f"I{clean}"
	return clean


def _parse_velocity_map(map_str):
	if not map_str:
		return None
	result = {}
	for item in map_str.split(","):
		parts = item.split(":")
		if len(parts) != 2:
			raise ValueError(f"Invalid velocity map entry: {item}")
		result[int(parts[0].strip())] = int(parts[1].strip())
	return result


def _write_int_array(f, name, values, indent=""):
	f.write(f"{indent}const int {name}[] = {{\n{indent}    ")
	for i, val in enumerate(values):
		f.write(f"{int(val)}")
		if i < len(values) - 1:
			f.write(", ")
		if (i + 1) % 12 == 0:
			f.write(f"\n{indent}    ")
	f.write(f"\n{indent}}};\n\n")


def _write_float_array(f, name, values, indent="", per_line=8, precision=7):
	f.write(f"{indent}const float {name}[] = {{\n{indent}    ")
	for i, val in enumerate(values):
		f.write(f"{val:.{precision}f}f")
		if i < len(values) - 1:
			f.write(", ")
		if (i + 1) % per_line == 0:
			f.write(f"\n{indent}    ")
	f.write(f"\n{indent}}};\n\n")


def _write_table(f, name, table, array_type, indent="", per_line=8, precision=7):
	n_notes, n_vels, n_parts = table.shape
	f.write(f"{indent}const {array_type} {name}[{n_notes}][{n_vels}] = {{\n")
	for i in range(n_notes):
		f.write(f"{indent}    {{\n")
		for j in range(n_vels):
			f.write(f"{indent}        {{")
			f.write("{")
			for k in range(n_parts):
				f.write(f"{table[i, j, k]:.{precision}f}f")
				if k < n_parts - 1:
					f.write(", ")
				if (k + 1) % per_line == 0 and k < n_parts - 1:
					f.write(f"\n{indent}            ")
			f.write("}")
			f.write("}")
			if j < n_vels - 1:
				f.write(",")
			f.write("\n")
		f.write(f"{indent}    }}")
		if i < n_notes - 1:
			f.write(",")
		f.write("\n")
	f.write(f"{indent}}};\n")



def _write_instrument_header(output_path, instrument_key, instrument_name,
						note_numbers, note_freqs, velocities, table, kmax_partials):
	prefix = f"k{instrument_key}"
	with open(output_path, "w", encoding="utf-8") as f:
		f.write("#pragma once\n\n")
		f.write("#include <cstddef>\n")
		f.write("#include \"SpectralCoef.h\"\n\n")
		f.write("// Generated by HarmonicTable.py\n")
		f.write(f"// Instrument: {instrument_name}\n")
		f.write(f"// Notes: {note_numbers.size}, Velocities: {velocities.size}, Partials: {kmax_partials}\n\n")

		f.write("namespace DSP\n{\n")
		f.write(f"    static constexpr int {prefix}TableNumNotes = {note_numbers.size};\n")
		f.write(f"    static constexpr int {prefix}TableNumVelocities = {velocities.size};\n")
		f.write(f"    static constexpr int {prefix}TableNumPartials = {kmax_partials};\n\n")

		_write_int_array(f, f"{prefix}TableNoteNumbers", note_numbers, indent="    ")
		_write_float_array(f, f"{prefix}TableNoteFreqHz", note_freqs, indent="    ")
		_write_int_array(f, f"{prefix}TableVelocities", velocities, indent="    ")

		_write_table(f, f"{prefix}PartialTable", table, array_type="CoefficientArray", indent="    ")
		f.write("}\n")


def _process_tinysol(args):
	csv_path = Path(args.tinysol_csv)
	if not csv_path.exists():
		print(f"TinySOL CSV not found: {csv_path}")
		return 1

	root_dir = Path(args.tinysol_root) if args.tinysol_root else csv_path.parent / "TinySOL"
	output_dir = Path(args.tinysol_output_dir) if args.tinysol_output_dir else csv_path.parent
	output_dir.mkdir(parents=True, exist_ok=True)

	vel_map = _parse_velocity_map(args.dyn_velocity_map)
	rows = []
	missing_files = 0

	with open(csv_path, "r", encoding="utf-8") as handle:
		reader = csv.DictReader(handle)
		for row in reader:
			path = row.get("Path")
			if not path:
				continue
			file_path = root_dir / path
			if not file_path.exists():
				file_path = csv_path.parent / path
			if not file_path.exists():
				missing_files += 1
				continue
			row["_file"] = file_path
			rows.append(row)

	if missing_files > 0:
		print(f"Warning: {missing_files} audio files listed in CSV were not found.")

	if not rows:
		print("No TinySOL rows available after filtering for existing audio files.")
		return 1

	instrument_filter = None
	if args.tinysol_instruments:
		instrument_filter = {item.strip() for item in args.tinysol_instruments.split(",") if item.strip()}

	grouped = {}
	for row in rows:
		instrument = row.get("Instrument (abbr.)", "")
		if instrument_filter and instrument not in instrument_filter:
			continue
		grouped.setdefault(instrument, []).append(row)

	if not grouped:
		print("No instruments matched the filter.")
		return 1

	all_dynamic_ids = set()
	for instrument_rows in grouped.values():
		for row in instrument_rows:
			if row.get("Dynamics ID") is not None:
				try:
					all_dynamic_ids.add(int(row["Dynamics ID"]))
				except ValueError:
					pass

	if args.fixed_velocity is None and vel_map is None:
		sorted_dyn_ids = sorted(all_dynamic_ids)
		vel_map = {dyn_id: int(args.dyn_vel_base + args.dyn_vel_step * idx)
				   for idx, dyn_id in enumerate(sorted_dyn_ids)}

	script_dir = Path(__file__).resolve().parent
	kmax_partials = _load_kmax_partials(script_dir)
	if kmax_partials is None:
		kmax_partials = args.max_partials

	if args.max_partials > kmax_partials:
		print(f"Requested max-partials {args.max_partials} exceeds kMaxPartials {kmax_partials}.")
		return 1

	for instrument, instrument_rows in grouped.items():
		by_note = {}
		for row in instrument_rows:
			try:
				midi_note = int(row.get("Pitch ID", ""))
				dyn_id = int(row.get("Dynamics ID", ""))
			except ValueError:
				continue

			if args.fixed_velocity is not None:
				velocity = int(args.fixed_velocity)
				by_note.setdefault(midi_note, {}).setdefault(velocity, []).append(row["_file"])
				continue

			velocity = vel_map.get(dyn_id) if vel_map is not None else None
			if velocity is None:
				continue

			by_note.setdefault(midi_note, {}).setdefault(velocity, []).append(row["_file"])

		if not by_note:
			continue

		note_numbers = np.array(sorted(by_note.keys()), dtype=np.int32)
		if args.fixed_velocity is not None:
			velocities = np.array([int(args.fixed_velocity)], dtype=np.int32)
		else:
			velocities = np.array(sorted({vel for note in by_note.values() for vel in note.keys()}), dtype=np.int32)
		note_freqs = np.array([_midi_to_freq(note) for note in note_numbers], dtype=np.float64)

		table = np.zeros((note_numbers.size, velocities.size, kmax_partials), dtype=np.float32)
		for note_idx, midi_note in enumerate(note_numbers):
			for vel_idx, velocity in enumerate(velocities):
				files = by_note.get(int(midi_note), {}).get(int(velocity), [])
				if not files:
					continue

				coeffs_list = []
				for file_path in files:
					sample_rate, audio = _read_wav_mono(file_path)
					coeffs = _analyze_partials(
						audio, _midi_to_freq(midi_note), sample_rate,
						args.max_partials, args.analysis_start, args.analysis_length,
						args.band_ratio, args.min_band_hz
					)
					coeffs = _normalize_coeffs(coeffs, args.normalize, args.min_db).astype(np.float32)
					coeffs_list.append(coeffs)

				if coeffs_list:
					avg_coeffs = np.mean(np.stack(coeffs_list, axis=0), axis=0)
					table[note_idx, vel_idx, :avg_coeffs.size] = avg_coeffs

		instrument_key = _sanitize_identifier(instrument)
		output_path = output_dir / f"HarmonicTable_{instrument_key}.h"
		_write_instrument_header(output_path, instrument_key, instrument,
							 note_numbers, note_freqs, velocities, table, kmax_partials)
		print(f"Wrote {instrument} table to {output_path}")

	return 0


def main():
	parser = argparse.ArgumentParser(
		description="Compute piano partial coefficients from a pickle dataset and export to a C constant file."
	)
	parser.add_argument("-i", "--input", help="Path to DatasetSingleNote_NF_fixed.pickle")
	parser.add_argument("-o", "--output", help="Path to output C/C++ header file")
	parser.add_argument("--wave-index", type=int, default=-1,
						help="Index in pickle list to use as waveform array (default: last)")
	parser.add_argument("--tinysol-csv", help="Path to TinySOL metadata CSV")
	parser.add_argument("--tinysol-root", help="Root directory for TinySOL audio (defaults to <csv>/TinySOL)")
	parser.add_argument("--tinysol-instruments",
						help="Comma-separated instrument abbreviations to include (e.g., Fl,TpC,Tbn)")
	parser.add_argument("--tinysol-output-dir",
						help="Output directory for per-instrument tables")
	parser.add_argument("--dyn-velocity-map",
						help="Explicit dynamics-to-velocity map, e.g. 0:40,1:60,2:80,3:100,4:120")
	parser.add_argument("--fixed-velocity", type=int,
						help="Force a single velocity value for all TinySOL dynamics (e.g., 75)")
	parser.add_argument("--dyn-vel-base", type=int, default=40,
						help="Base velocity for lowest dynamics ID (default: 40)")
	parser.add_argument("--dyn-vel-step", type=int, default=20,
						help="Velocity step per dynamics ID (default: 20)")
	parser.add_argument("--sample-rate", type=float, default=48000.0,
						help="Sample rate used by the dataset (default: 48000)")
	parser.add_argument("--max-partials", type=int, default=200,
						help="Number of partials to compute (default: 200)")
	parser.add_argument("--analysis-start", type=float, default=0.2,
						help="Start fraction of the note to analyze (default: 0.2)")
	parser.add_argument("--analysis-length", type=float, default=0.5,
						help="Fractional length to analyze (default: 0.5)")
	parser.add_argument("--band-ratio", type=float, default=0.35,
						help="Band half-width as ratio of f0 (default: 0.35)")
	parser.add_argument("--min-band-hz", type=float, default=5.0,
						help="Minimum band half-width in Hz (default: 5.0)")
	parser.add_argument("--normalize", choices=["max", "fundamental", "none"], default="max",
						help="Normalization mode for coefficients (default: max)")
	parser.add_argument("--min-db", type=float, default=-80.0,
						help="Clamp coefficients below this dB (default: -80)")
	parser.add_argument("--table-name", default="kPianoPartialTable",
						help="Name of the C array (default: kPianoPartialTable)")

	args = parser.parse_args()

	if args.tinysol_csv:
		return _process_tinysol(args)

	if not args.input or not args.output:
		print("--input and --output are required unless --tinysol-csv is provided.")
		return 1

	input_path = Path(args.input)
	if not input_path.exists():
		print(f"Input file not found: {input_path}")
		return 1

	with open(input_path, "rb") as handle:
		dataset = pickle.load(handle)

	if not isinstance(dataset, list) or len(dataset) == 0:
		print("Unexpected dataset format: expected a list with waveform data.")
		return 1

	wave_index = args.wave_index
	if wave_index < 0:
		wave_index = len(dataset) + wave_index

	if wave_index < 0 or wave_index >= len(dataset):
		print(f"wave-index {args.wave_index} out of range for dataset size {len(dataset)}")
		return 1

	waveforms = dataset[wave_index]
	if not isinstance(waveforms, np.ndarray) or waveforms.ndim != 2:
		print("Selected wave-index does not point to a 2D waveform array.")
		return 1

	num_rows = waveforms.shape[0]
	f0_list = _derive_f0_list(dataset, num_rows)
	vel_list = _derive_velocity_list(dataset, num_rows)

	script_dir = Path(__file__).resolve().parent
	kmax_partials = _load_kmax_partials(script_dir)
	if kmax_partials is None:
		kmax_partials = args.max_partials

	if args.max_partials > kmax_partials:
		print(f"Requested max-partials {args.max_partials} exceeds kMaxPartials {kmax_partials}.")
		return 1

	f0_keys = np.round(f0_list, 3)
	vel_keys = np.round(vel_list, 1)
	unique_f0 = np.unique(f0_keys[f0_keys > 0.0])
	unique_vel = np.unique(vel_keys[vel_keys > 0.0])

	note_map = {val: idx for idx, val in enumerate(sorted(unique_f0))}
	vel_map = {val: idx for idx, val in enumerate(sorted(unique_vel))}

	note_freqs = np.array(sorted(unique_f0), dtype=np.float64)
	note_numbers = np.array([_freq_to_midi(freq) for freq in note_freqs], dtype=np.int32)
	velocities = np.array(sorted(unique_vel), dtype=np.int32)

	table = np.zeros((note_freqs.size, velocities.size, kmax_partials), dtype=np.float32)
	missing_pairs = 0

	for i in range(num_rows):
		f0 = float(f0_keys[i])
		vel = float(vel_keys[i])
		if f0 <= 0.0 or vel <= 0.0:
			missing_pairs += 1
			continue

		note_idx = note_map.get(f0)
		vel_idx = vel_map.get(vel)
		if note_idx is None or vel_idx is None:
			missing_pairs += 1
			continue

		coeffs = _analyze_partials(
			waveforms[i], f0, args.sample_rate, args.max_partials,
			args.analysis_start, args.analysis_length, args.band_ratio, args.min_band_hz
		)
		coeffs = _normalize_coeffs(coeffs, args.normalize, args.min_db).astype(np.float32)
		table[note_idx, vel_idx, :coeffs.size] = coeffs

	output_path = Path(args.output)
	output_path.parent.mkdir(parents=True, exist_ok=True)

	with open(output_path, "w", encoding="utf-8") as f:
		f.write("#pragma once\n\n")
		f.write("#include <cstddef>\n")
		f.write("#include \"SpectralCoef.h\"\n\n")
		f.write("// Generated by HarmonicTable.py\n")
		f.write(f"// Notes: {note_freqs.size}, Velocities: {velocities.size}, Partials: {args.max_partials}\n\n")

		f.write("namespace DSP\n{\n")
		f.write(f"    static constexpr int kPianoTableNumNotes = {note_freqs.size};\n")
		f.write(f"    static constexpr int kPianoTableNumVelocities = {velocities.size};\n")
		f.write(f"    static constexpr int kPianoTableNumPartials = {kmax_partials};\n\n")

		_write_int_array(f, "kPianoTableNoteNumbers", note_numbers, indent="    ")
		_write_float_array(f, "kPianoTableNoteFreqHz", note_freqs, indent="    ")
		_write_int_array(f, "kPianoTableVelocities", velocities, indent="    ")

		_write_table(f, args.table_name, table, array_type="CoefficientArray", indent="    ")
		f.write("}\n")
	print(f"Wrote table to {output_path}")
	if missing_pairs > 0:
		print(f"Warning: {missing_pairs} rows had missing note/velocity values and were skipped")

	return 0


if __name__ == "__main__":
	sys.exit(main())
