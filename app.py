
from flask import Flask, send_from_directory

app = Flask(__name__)

# Dashboard
@app.route("/")
def dashboard():
    return send_from_directory(".", "index.html")

# CSS
@app.route("/style.css")
def stylesheet():
    return send_from_directory(".", "style.css")

# JavaScript
@app.route("/script.js")
def javascript():
    return send_from_directory(".", "script.js")


if __name__ == "__main__":
    print("ADPG Monitoring Dashboard")
    print("Dashboard: http://127.0.0.1:5000")

    app.run(
        host="0.0.0.0",
        port=5000,
        debug=False,
        use_reloader=False
    )
