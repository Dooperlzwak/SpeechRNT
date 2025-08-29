#!/usr/bin/env python3

import os
import sys
import argparse
from pathlib import Path
from huggingface_hub import snapshot_download

def download_model(model_name: str, output_dir: str):
    """Download a model from Hugging Face Hub."""
    try:
        print(f"Downloading {model_name} to {output_dir}...")
        
        # Create output directory
        Path(output_dir).mkdir(parents=True, exist_ok=True)
        
        # Download the model
        snapshot_download(
            repo_id=model_name,
            local_dir=output_dir,
            local_dir_use_symlinks=False,
            resume_download=True
        )
        
        print(f"Successfully downloaded {model_name}")
        
    except Exception as e:
        print(f"Error downloading {model_name}: {e}")
        sys.exit(1)

def main():
    parser = argparse.ArgumentParser(description="Download AI models from Hugging Face Hub")
    parser.add_argument("--model", required=True, help="Model name on Hugging Face Hub")
    parser.add_argument("--output", required=True, help="Output directory name")
    
    args = parser.parse_args()
    
    # Get base models directory
    models_dir = os.environ.get("MODELS_DIR", "/models")
    output_path = os.path.join(models_dir, args.output)
    
    download_model(args.model, output_path)

if __name__ == "__main__":
    main()