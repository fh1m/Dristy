#!/usr/bin/env python3
"""
Dristy Model Training Pipeline
===============================

End-to-end pipeline: dataset → train → convert → flash

Supports:
  - MobileNet-YOLOv2 (object detection, default)
  - MobileNet (classification)
  - Custom YOLO anchors

Requirements:
  pip install aXeleRate tensorflow==2.x nncase==0.2.0b4

Usage:
  # Train a custom detector
  python3 dristy_model_pipeline.py train \
    --dataset /path/to/voc_dataset \
    --classes person,car,drone \
    --input-size 320x256 \
    --epochs 100

  # Convert a Keras model to kmodel
  python3 dristy_model_pipeline.py convert \
    --model model.h5 \
    --output model.kmodel \
    --target k210 \
    --input-size 320x256

  # Flash model to device
  python3 dristy_model_pipeline.py flash \
    --model model.kmodel \
    --slot 0 \
    --port /dev/ttyUSB0

  # Full pipeline: train + convert + flash
  python3 dristy_model_pipeline.py full \
    --dataset /path/to/voc_dataset \
    --classes person,car,drone \
    --slot 0 --port /dev/ttyUSB0
"""

import argparse
import json
import os
import struct
import subprocess
import sys
from pathlib import Path

# Flash slot addresses (must match dristy_model.h)
FLASH_SLOTS = {
    0: 0x100000,  # 2 MB — default detector
    1: 0x300000,  # 1 MB — face detector
    2: 0x400000,  # 1 MB — classifier
    3: 0x500000,  # 2 MB — user custom
}

SLOT_SIZES = {
    0: 0x200000,
    1: 0x100000,
    2: 0x100000,
    3: 0x200000,
}


def generate_axelerate_config(args):
    """Generate aXeleRate training config JSON for MobileNet-YOLOv2."""
    classes = args.classes.split(",")
    w, h = args.input_size.split("x")

    config = {
        "model": {
            "type": "Detector",
            "architecture": "MobileNet1_0",
            "input_size": [int(w), int(h)],
            "anchors": [
                0.57273, 0.677385,
                1.87446, 2.06253,
                3.33843, 5.47434,
                7.88282, 3.52778,
                9.77052, 9.16828,
            ],
            "labels": classes,
            "obj_thresh": 0.3,
            "iou_thresh": 0.3,
            "coord_scale": 1.0,
            "object_scale": 5.0,
            "no_object_scale": 1.0,
            "class_scale": 1.0,
        },
        "pretrained": {
            "full": "",
        },
        "train": {
            "actual_epoch": int(args.epochs),
            "train_image_folder": os.path.join(args.dataset, "JPEGImages"),
            "train_annot_folder": os.path.join(args.dataset, "Annotations"),
            "train_times": 4,
            "valid_image_folder": os.path.join(args.dataset, "JPEGImages"),
            "valid_annot_folder": os.path.join(args.dataset, "Annotations"),
            "valid_times": 1,
            "batch_size": 8,
            "learning_rate": 1e-4,
            "saved_folder": args.output_dir,
            "first_trainable_layer": "",
            "augumentation": True,
        },
        "converter": {
            "type": ["k210"],
        },
    }

    config_path = os.path.join(args.output_dir, "axelerate_config.json")
    os.makedirs(args.output_dir, exist_ok=True)
    with open(config_path, "w") as f:
        json.dump(config, f, indent=2)

    print(f"[DRISTY] Config written to {config_path}")
    return config_path


def cmd_train(args):
    """Train a model using aXeleRate."""
    config_path = generate_axelerate_config(args)

    print("[DRISTY] Starting aXeleRate training...")
    print(f"  Dataset:    {args.dataset}")
    print(f"  Classes:    {args.classes}")
    print(f"  Input size: {args.input_size}")
    print(f"  Epochs:     {args.epochs}")
    print(f"  Output:     {args.output_dir}")
    print()

    try:
        from axelerate import setup_training
        setup_training(config_file=config_path)
    except ImportError:
        print("[DRISTY] aXeleRate not installed. Install with:")
        print("  pip install aXeleRate")
        print()
        print("Or train manually and convert with:")
        print(f"  python3 {sys.argv[0]} convert --model <your_model.h5> ...")
        return 1

    # Find the output .h5 model
    h5_files = list(Path(args.output_dir).glob("**/*.h5"))
    if h5_files:
        print(f"[DRISTY] Model saved: {h5_files[-1]}")
    return 0


def cmd_convert(args):
    """Convert a Keras/TFLite model to K210 kmodel using nncase."""
    w, h = args.input_size.split("x")

    print(f"[DRISTY] Converting {args.model} → {args.output}")
    print(f"  Target:     {args.target}")
    print(f"  Input size: {w}×{h}")

    # Method 1: Use nncase Python API (v0.2.0-beta4)
    try:
        import nncase

        compile_options = nncase.CompileOptions()
        compile_options.target = args.target
        compile_options.input_type = "uint8"
        compile_options.input_shape = [1, 3, int(h), int(w)]
        compile_options.input_range = [0, 255]
        compile_options.mean = [128.0, 128.0, 128.0]
        compile_options.std = [128.0, 128.0, 128.0]
        compile_options.output_type = "float32"

        compiler = nncase.Compiler(compile_options)

        # Import model
        if args.model.endswith(".tflite"):
            with open(args.model, "rb") as f:
                compiler.import_tflite(f.read())
        elif args.model.endswith(".onnx"):
            with open(args.model, "rb") as f:
                compiler.import_onnx(f.read())
        else:
            print(f"[DRISTY] Unsupported format: {args.model}")
            print("  Supported: .tflite, .onnx")
            return 1

        compiler.compile()
        kmodel = compiler.gencode_tobytes()

        with open(args.output, "wb") as f:
            f.write(kmodel)

        print(f"[DRISTY] kmodel written: {args.output} ({len(kmodel)} bytes)")
        return 0

    except ImportError:
        pass

    # Method 2: Use nncase CLI
    nncase_path = os.environ.get("NNCASE_PATH", "ncc")
    cmd = [
        nncase_path, "compile",
        args.model,
        args.output,
        "-i", args.target,
        "--input-type", "uint8",
        f"--input-shape", f"1,3,{h},{w}",
        "--mean", "128", "--std", "128",
    ]

    print(f"[DRISTY] Running: {' '.join(cmd)}")
    try:
        subprocess.run(cmd, check=True)
        print(f"[DRISTY] kmodel written: {args.output}")
        return 0
    except (FileNotFoundError, subprocess.CalledProcessError) as e:
        print(f"[DRISTY] nncase not found or failed: {e}")
        print()
        print("Install nncase v0.2.0-beta4:")
        print("  pip install nncase==0.2.0b4")
        print("  Or download from: https://github.com/kendryte/nncase/releases/tag/v0.2.0-beta4")
        return 1


def cmd_flash(args):
    """Flash a kmodel to a device slot."""
    slot = int(args.slot)
    if slot not in FLASH_SLOTS:
        print(f"[DRISTY] Invalid slot {slot}. Valid: {list(FLASH_SLOTS.keys())}")
        return 1

    flash_addr = FLASH_SLOTS[slot]
    max_size = SLOT_SIZES[slot]

    model_size = os.path.getsize(args.model)
    if model_size > max_size:
        print(f"[DRISTY] Model too large: {model_size} bytes > slot max {max_size} bytes")
        return 1

    print(f"[DRISTY] Flashing {args.model} ({model_size} bytes)")
    print(f"  Slot:    {slot}")
    print(f"  Address: 0x{flash_addr:06X}")
    print(f"  Port:    {args.port}")

    # Use kflash.py (bundled with K210 toolchain)
    kflash = os.environ.get("KFLASH_PATH", "kflash")
    cmd = [
        kflash,
        "-p", args.port,
        "-b", str(args.baud),
        "-a", str(flash_addr),
        args.model,
    ]

    print(f"[DRISTY] Running: {' '.join(cmd)}")
    try:
        subprocess.run(cmd, check=True)
        print(f"[DRISTY] Flash complete!")
        return 0
    except (FileNotFoundError, subprocess.CalledProcessError) as e:
        print(f"[DRISTY] kflash failed: {e}")
        print("Install kflash_py: pip install kflash")
        return 1


def cmd_metadata(args):
    """Generate a Dristy model metadata JSON for a kmodel."""
    classes = args.classes.split(",") if args.classes else []
    w, h = args.input_size.split("x")

    meta = {
        "dristy_model_version": 1,
        "name": args.name or os.path.basename(args.model).replace(".kmodel", ""),
        "input_width": int(w),
        "input_height": int(h),
        "input_channels": 3,
        "post_type": args.post_type,
        "num_classes": len(classes),
        "classes": classes,
        "confidence_threshold": 0.3,
        "nms_threshold": 0.3,
        "anchors": [
            1.08, 1.19, 3.42, 4.41, 6.63, 11.38, 9.42, 5.11, 16.62, 10.52
        ],
    }

    output = args.model.replace(".kmodel", ".json")
    with open(output, "w") as f:
        json.dump(meta, f, indent=2)

    print(f"[DRISTY] Metadata written: {output}")
    return 0


def cmd_full(args):
    """Full pipeline: train → convert → flash."""
    ret = cmd_train(args)
    if ret != 0:
        return ret

    # Find the trained model
    h5_files = list(Path(args.output_dir).glob("**/*.h5"))
    if not h5_files:
        print("[DRISTY] No .h5 model found after training")
        return 1

    args.model = str(h5_files[-1])
    args.output = str(h5_files[-1]).replace(".h5", ".kmodel")
    args.target = "k210"
    ret = cmd_convert(args)
    if ret != 0:
        return ret

    args.model = args.output
    return cmd_flash(args)


def main():
    parser = argparse.ArgumentParser(
        description="Dristy Model Training Pipeline for K210/HuskyLens",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    sub = parser.add_subparsers(dest="command")

    # Train
    p_train = sub.add_parser("train", help="Train a model with aXeleRate")
    p_train.add_argument("--dataset", required=True, help="PASCAL VOC dataset path")
    p_train.add_argument("--classes", required=True, help="Comma-separated class names")
    p_train.add_argument("--input-size", default="320x256", help="W×H (default: 320x256)")
    p_train.add_argument("--epochs", default=100, type=int)
    p_train.add_argument("--output-dir", default="./dristy_models")

    # Convert
    p_conv = sub.add_parser("convert", help="Convert model to kmodel")
    p_conv.add_argument("--model", required=True, help=".tflite or .onnx model")
    p_conv.add_argument("--output", required=True, help="Output .kmodel path")
    p_conv.add_argument("--target", default="k210", help="Target (default: k210)")
    p_conv.add_argument("--input-size", default="320x256")

    # Flash
    p_flash = sub.add_parser("flash", help="Flash kmodel to device")
    p_flash.add_argument("--model", required=True, help=".kmodel file")
    p_flash.add_argument("--slot", required=True, type=int, help="Flash slot (0-3)")
    p_flash.add_argument("--port", default="/dev/ttyUSB0")
    p_flash.add_argument("--baud", default=115200, type=int)

    # Metadata
    p_meta = sub.add_parser("metadata", help="Generate model metadata JSON")
    p_meta.add_argument("--model", required=True, help=".kmodel file")
    p_meta.add_argument("--classes", help="Comma-separated class names")
    p_meta.add_argument("--name", help="Model display name")
    p_meta.add_argument("--input-size", default="320x256")
    p_meta.add_argument("--post-type", default="yolov2",
                        choices=["none", "yolov2", "classify", "face", "segment", "custom"])

    # Full pipeline
    p_full = sub.add_parser("full", help="Train + convert + flash")
    p_full.add_argument("--dataset", required=True)
    p_full.add_argument("--classes", required=True)
    p_full.add_argument("--input-size", default="320x256")
    p_full.add_argument("--epochs", default=100, type=int)
    p_full.add_argument("--output-dir", default="./dristy_models")
    p_full.add_argument("--slot", default=0, type=int)
    p_full.add_argument("--port", default="/dev/ttyUSB0")
    p_full.add_argument("--baud", default=115200, type=int)

    args = parser.parse_args()

    if not args.command:
        parser.print_help()
        return 1

    commands = {
        "train": cmd_train,
        "convert": cmd_convert,
        "flash": cmd_flash,
        "metadata": cmd_metadata,
        "full": cmd_full,
    }

    return commands[args.command](args)


if __name__ == "__main__":
    sys.exit(main() or 0)
