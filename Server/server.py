# CONFIG
HOST                  = "0.0.0.0"
PORT                  = 8888
SOURCE_LANG           = "es"
DEST_LANG             = "en" 
USE_GPU               = True
BATCH_SIZE            = 1
WORKERS               = 4
# Merges overlappings on the X axis. Sometimes better, sometimes worse.
MERGE_X_OVERLAPPING   = False
MERGE_MAX_Y_DIFF      = 20
REPORT_FILE_PATH      = "report.txt"
# END OF CONFIG

from http.server import BaseHTTPRequestHandler, HTTPServer
from deep_translator import GoogleTranslator
import easyocr
import json
import os

class TranslatorRequestHandler(BaseHTTPRequestHandler):
    reader = None
    translator = None

    def process_image(self, post_body):
        if self.reader == None:
            self.reader = easyocr.Reader([SOURCE_LANG, "en"], gpu = USE_GPU)
        
        return self.reader.readtext(post_body, batch_size=BATCH_SIZE, workers=WORKERS)
    
    # item = [ box [ p1 [ x, y ], p2 [ x, y ], p3 [ x, y ], p4 [ x, y ] ], text, confidence ]
    def map_item(self, item):
        x = int(item[0][0][0])
        y = int(item[0][0][1])

        vx = int(item[0][2][0])
        vy = int(item[0][2][1])
        
        w = int(vx - x)
        h = int(vy - y)

        source_text = item[1]
        confidence = item[2]

        return x, y, w, h, source_text, confidence

    def create_entry(self, x, y, w, h, message, translation):
        entry = {}

        entry["x"] = int(x)
        entry["y"] = int(y)
        entry["w"] = int(w)
        entry["h"] = int(h)
        entry["message"] = message
        entry["translation"] = translation

        return entry

    def translate(self, text):
        if self.translator == None:
            self.translator = GoogleTranslator(source="auto", target=DEST_LANG)
        
        return self.translator.translate(text)

    def process_item(self, item):
        x, y, w, h, source_text, confidence = self.map_item(item)

        if confidence < 0.2:
            return None

        translated_text = self.translate(source_text)

        if not translated_text or len(translated_text) <= 1:
            translated_text = ""

        if len(source_text) > 0:
            return self.create_entry(x, y, w, h, source_text, translated_text)
        
        return None

    def process_items(self, items):
        entries = []

        for item in items:
            entry = self.process_item(item)

            if entry:
                entries.append(entry)
        
        return entries
    
    def merge_x_overlapping_entries(self, entries):
        if not MERGE_X_OVERLAPPING:
            return entries

        for entry_source in entries:
            for entry in entries:
                if entry == entry_source:
                    continue
                
                if abs(entry_source["y"] - entry["y"]) < MERGE_MAX_Y_DIFF and \
                    entry["x"] < entry_source["x"] + entry_source["w"] and \
                    entry["x"] + entry["w"] > entry_source["x"] and \
                    entry["y"] < entry_source["y"] + entry_source["h"] and \
                    entry["y"] + entry["h"] > entry_source["y"]:

                    entry_source["message"] = "{} {}".format(entry_source["message"], entry["message"])
                    entry_source["translation"] = "{} {}".format(entry_source["translation"], entry["translation"])
                    entry_source["w"] += entry["w"]
                    entry["message"] = ""
        
        entries = [entry for entry in entries if len(entry["message"])]

        return entries
    
    def dump_entries(self, entries):
        for entry in entries:
            with open(REPORT_FILE_PATH, "a") as f:
                f.write("{}\n{}\n\n".format(entry["message"], entry["translation"]))
            
            print("{} -> {}".format(entry["message"], entry["translation"]))

    def create_entries(self, post_body):
        items = self.process_image(post_body)
        entries = self.process_items(items)
        entries = self.merge_x_overlapping_entries(entries)

        self.dump_entries(entries)
        
        return entries

    def do_GET(self):
        self.send_response(404)
        self.end_headers()

    def do_PUT(self):
        self.send_response(404)
        self.end_headers()

    def do_POST(self):
        self.send_response(200)
        self.send_header("Content-type", "application/json")
        self.end_headers()

        content_len = int(self.headers.get("content-length", 0))
        req_body = self.rfile.read(content_len)
        entries = self.create_entries(req_body)
        resp_body = bytes(json.dumps(entries), "utf-8")

        self.wfile.write(resp_body)
        self.wfile.flush()

def main():
    if not os.path.exists(REPORT_FILE_PATH):
        os.mknod(REPORT_FILE_PATH)
    else:
        print("Report exists, resuming")

    print("Listennig on {}:{}".format(HOST, PORT))

    HTTPServer((HOST, PORT), TranslatorRequestHandler).serve_forever()

if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("Interrupted, exitting")