from flask import Flask, request, jsonify
import subprocess
import os
from roboflow import Roboflow
import tempfile
import logging
import glob
import shutil
import concurrent.futures
from concurrent.futures import ThreadPoolExecutor

logging.basicConfig(level=os.environ.get('LOG_LEVEL', 'INFO'), format='%(asctime)s - %(levelname)s - %(message)s')

app = Flask(__name__)

# Roboflow API configuration
ROBOFLOW_API_KEY = "xxxxxxxxxxxxxxxxx"
ROBOFLOW_WORKSPACE = "greenhouse-vsal1"
ROBOFLOW_PROJECT = "esp32-czges"
ROBOFLOW_VERSION = 1

confidence_threshold = 40
overlap_threshold = 30

rf = Roboflow(api_key=ROBOFLOW_API_KEY)
project = rf.workspace(ROBOFLOW_WORKSPACE).project(ROBOFLOW_PROJECT)
model = project.version(ROBOFLOW_VERSION).model

@app.route("/infer", methods=["POST"])
def upload_and_infer():
    if "file" not in request.files:
        return jsonify({"error": "No file part"}), 400

    # Parse optional parameters (confidence, overlap , Default values will be used if not provided)
    try:
        confidence = float(request.form["confidence"]) if "confidence" in request.form else confidence_threshold
        overlap = float(request.form["overlap"]) if "overlap" in request.form else overlap_threshold
    except ValueError:
        return jsonify({"error": "Invalid parameter values. Parameters must be numbers"}), 400
    
    logging.info(f"Processing with confidence: {confidence}, overlap: {overlap}")

    file = request.files["file"]
    file_data = file.read()
    logging.info(f"Processing file: {file.filename}")

    try:
        if file.filename.endswith(".h264"):
            frame_count, predictions = process_h264(file_data, confidence, overlap)
        elif file.filename.lower().endswith(('.png', '.jpg', '.jpeg')):
            frame_count, predictions = process_image(file_data, confidence, overlap)
        else:
            return jsonify({"error": "Unsupported file type"}), 400

        if "error" in predictions:
            logging.error(f"Processing error: {predictions['error']}")
            return jsonify(predictions), 500

        # Calculate total objects detected
        total_objects = sum(predictions["class_counts"].values())
        
        response_data = {
            "frame_count": frame_count,
            "total_objects": total_objects,
            "predictions": predictions["class_counts"]
        }
        
        logging.info(f"Successfully processed {file.filename}. Found {total_objects} objects in {frame_count} frames")
        return jsonify(response_data)
    
    except Exception as e:
        logging.error(f"Unexpected error processing {file.filename}: {str(e)}", exc_info=True)
        return jsonify({"error": f"Unexpected error: {str(e)}"}), 500


def process_image(image_data, confidence=40, overlap=30):
    logging.info(f"Processing single image with confidence: {confidence}, overlap: {overlap}")
    try:
        # Create temporary file for the image
        with tempfile.NamedTemporaryFile(suffix='.jpg', delete=False) as temp_file:
            temp_file.write(image_data)
            temp_path = temp_file.name
        
        try:
            # Add debug logging for prediction params
            logging.info(f"Calling model.predict with confidence={confidence}, overlap={overlap}")
            predictions = model.predict(temp_path, confidence=confidence, overlap=overlap).json()
            logging.info(f"Raw predictions with confidence {confidence}: {predictions}")
            
            # Result class counts
            results = {
                "class_counts": {}
            }
            
            # Count predictions by class
            for prediction in predictions.get("predictions", []):
                class_name = prediction.get("class")
                confidence = prediction.get("confidence", 0)
                if (class_name):
                    results["class_counts"][class_name] = results["class_counts"].get(class_name, 0) + 1
                    logging.debug(f"Detected {class_name} with confidence {confidence}")
        finally:
            # Clean up temporary file
            os.remove(temp_path)
        
        return 1, results

    except Exception as e:
        logging.error("Image processing error", exc_info=True)
        return 0, {"error": f"Error during inference: {str(e)}"}

def process_frame(frame_path, confidence=40, overlap=30):
    try:
        predictions = model.predict(frame_path, confidence=confidence, overlap=overlap).json()
        return predictions.get("predictions", [])
    except Exception as e:
        logging.error(f"Error processing frame {frame_path}: {str(e)}")
        return []

def process_h264(video_data, confidence=40, overlap=30):
    temp_dir = None
    try:
        temp_dir = tempfile.mkdtemp()
        video_path = os.path.join(temp_dir, "temp.h264")
        
        with open(video_path, "wb") as f:
            f.write(video_data)

        # FFMPEG command to extract all I-frames
        cmd = [
            'ffmpeg',
            '-i', video_path.replace('\\', '/'),
            '-vf', "select='eq(pict_type,I)'",
            '-vsync', '0',
            '-frame_pts', '1',
            os.path.join(temp_dir, "frame_%04d.jpg")
        ]
        
        result = subprocess.run(cmd, capture_output=True, text=True)
        if result.returncode != 0:
            logging.error(f"FFmpeg error: {result.stderr}")
            raise Exception(f"FFmpeg failed with error: {result.stderr}")
            
        logging.info(f"FFmpeg extraction completed with return code: {result.returncode}")
        
        # List all extracted files for debugging
        all_files = os.listdir(temp_dir)
        frame_files = [f for f in all_files if f.startswith('frame_') and f.endswith('.jpg')]
        logging.info(f"Found {len(frame_files)} frame files in temp directory")
        
        frame_count = 0
        results = {
            "class_counts": {}
        }

        frame_pattern = os.path.join(temp_dir, "frame_*.jpg")
        frames = sorted(glob.glob(frame_pattern))
        logging.info(f"Processing {len(frames)} frames using concurrent execution")

        # Process frames in parallel with max 4 workers
        with ThreadPoolExecutor(max_workers=4) as executor:
            future_to_frame = {executor.submit(process_frame, frame, confidence, overlap): frame for frame in frames}
            
            for future in concurrent.futures.as_completed(future_to_frame):
                frame = future_to_frame[future]
                try:
                    predictions = future.result()
                    for prediction in predictions:
                        class_name = prediction.get("class")
                        if class_name:
                            results["class_counts"][class_name] = results["class_counts"].get(class_name, 0) + 1
                except Exception as e:
                    logging.error(f"Error processing frame {frame}: {str(e)}")

        frame_count = len(frames)
        if frame_count == 0:
            raise Exception("No frames were extracted from the video")

        logging.info(f"Processed {frame_count} frames with results: {results}")
        return frame_count, results

    except Exception as e:
        logging.error(f"Video processing error: {str(e)}", exc_info=True)
        return 0, {"error": f"Error processing video: {str(e)}"}

    finally:
        if temp_dir and os.path.exists(temp_dir):
            try:
                shutil.rmtree(temp_dir)
            except Exception as e:
                logging.error(f"Error cleaning up temp files: {str(e)}")


if __name__ == "__main__":
    app.run(host="0.0.0.0", port=int(os.environ.get("PORT", 8080)))