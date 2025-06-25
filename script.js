const youtubeUrlInput = document.getElementById('youtubeUrl');
const fetchBtn = document.getElementById('fetchBtn');
const statusDiv = document.getElementById('status');
const videoInfoDiv = document.getElementById('videoInfo');
const videoTitleH2 = document.getElementById('videoTitle');
const videoAuthorSpan = document.getElementById('videoAuthor');
const videoDurationSpan = document.getElementById('videoDuration');
const muxedFormatsListUl = document.getElementById('muxedFormatsList');
const adaptiveFormatsListUl = document.getElementById('adaptiveFormatsList');

let wasmModule = null;

// Check if ytWasmModule is defined (it's the EXPORT_NAME from Emscripten)
if (typeof ytWasmModule === 'undefined') {
    statusDiv.textContent = 'Error: Wasm module (ytWasmModule) not found. Check build and script path.';
    console.error('ytWasmModule is not defined. Ensure Emscripten output is correctly loaded.');
} else {
    // Initialize the Wasm module
    ytWasmModule().then(module => {
        wasmModule = module;
        statusDiv.textContent = 'Wasm module loaded successfully.';
        fetchBtn.disabled = false;
    }).catch(e => {
        statusDiv.textContent = `Error loading Wasm module: ${e}`;
        console.error("Wasm module loading error:", e);
    });
    fetchBtn.disabled = true; // Disable until module is loaded
}


fetchBtn.addEventListener('click', async () => {
    if (!wasmModule) {
        statusDiv.textContent = 'Wasm module not ready.';
        return;
    }
    const url = youtubeUrlInput.value.trim();
    if (!url) {
        statusDiv.textContent = 'Please enter a YouTube URL.';
        return;
    }

    statusDiv.textContent = 'Fetching video info...';
    videoInfoDiv.classList.add('hidden');
    muxedFormatsListUl.innerHTML = '';
    adaptiveFormatsListUl.innerHTML = '';

    try {
        // Allocate memory for the URL string in Wasm heap
        const urlPtr = wasmModule.stringToUTF8(url);
        const resultPtr = wasmModule._get_video_info_json(urlPtr);
        wasmModule._free(urlPtr); // Free the allocated URL string

        const resultJsonStr = wasmModule.UTF8ToString(resultPtr);
        wasmModule._free_c_string(resultPtr); // Free the result string from C++

        const result = JSON.parse(resultJsonStr);

        if (result.success) {
            displayVideoInfo(result.data);
            statusDiv.textContent = 'Video info loaded.';
        } else {
            statusDiv.textContent = `Error: ${result.error}`;
        }
    } catch (e) {
        statusDiv.textContent = `Error calling Wasm function: ${e}`;
        console.error("Wasm call error:", e);
    }
});

function displayVideoInfo(data) {
    videoTitleH2.textContent = data.title || 'N/A';
    videoAuthorSpan.textContent = data.author || 'N/A';
    videoDurationSpan.textContent = data.lengthSeconds || 'N/A';

    populateFormatList(data.formats, muxedFormatsListUl, data.id);
    populateFormatList(data.adaptiveFormats, adaptiveFormatsListUl, data.id);

    videoInfoDiv.classList.remove('hidden');
}

function populateFormatList(formats, listElement, videoId) {
    listElement.innerHTML = ''; // Clear previous entries
    if (!formats || formats.length === 0) {
        listElement.innerHTML = '<li>No formats of this type available.</li>';
        return;
    }

    formats.forEach(format => {
        const li = document.createElement('li');
        let text = `itag: ${format.itag}`;
        if (format.qualityLabel) text += ` | ${format.qualityLabel}`;
        else if (format.width && format.height) text += ` | ${format.width}x${format.height}${format.fps ? 'p'+format.fps : 'p'}`;

        if (format.isAudioOnly) text += ` | Audio (${format.audioQuality || 'N/A'})`;
        else if (format.isVideoOnly) text += ` | Video Only`;
        else text += ` | Muxed A/V`;

        if (format.codecs) text += ` (${format.codecs.split('.')[0]})`; // Show main codec part
        if (format.contentLength) text += ` | Size: ${formatBytes(format.contentLength)}`;

        const downloadButton = document.createElement('button');
        downloadButton.textContent = 'Get Download Link';
        downloadButton.onclick = async () => {
            statusDiv.textContent = `Getting link for itag ${format.itag}...`;
            try {
                const videoUrlForApi = youtubeUrlInput.value.trim(); // Use current URL from input
                const urlPtr = wasmModule.stringToUTF8(videoUrlForApi);
                const itag = format.itag;

                const streamInfoPtr = wasmModule._get_stream_url_json(urlPtr, itag);
                wasmModule._free(urlPtr);

                const streamInfoJsonStr = wasmModule.UTF8ToString(streamInfoPtr);
                wasmModule._free_c_string(streamInfoPtr);

                const streamInfo = JSON.parse(streamInfoJsonStr);

                if (streamInfo.success) {
                    statusDiv.textContent = `Link ready for ${streamInfo.suggested_filename}. Click to download.`;
                    const a = document.createElement('a');
                    a.href = streamInfo.url;
                    a.download = streamInfo.suggested_filename;
                    a.textContent = `Download ${streamInfo.suggested_filename}`;
                    a.style.display = 'block'; // Make it visible
                    a.style.margin = '10px 0';
                    // Insert link below status or replace status
                    statusDiv.innerHTML = '';
                    statusDiv.appendChild(a);
                    // Or trigger download automatically: a.click();
                } else {
                    statusDiv.textContent = `Error getting stream URL: ${streamInfo.error}`;
                }
            } catch (e) {
                statusDiv.textContent = `Error calling Wasm for stream URL: ${e}`;
                console.error("Wasm call for stream URL error:", e);
            }
        };

        li.appendChild(document.createTextNode(text + " "));
        li.appendChild(downloadButton);
        listElement.appendChild(li);
    });
}

function formatBytes(bytes, decimals = 2) {
    if (bytes === 0 || !bytes) return '0 Bytes';
    const k = 1024;
    const dm = decimals < 0 ? 0 : decimals;
    const sizes = ['Bytes', 'KB', 'MB', 'GB', 'TB'];
    const i = Math.floor(Math.log(bytes) / Math.log(k));
    return parseFloat((bytes / Math.pow(k, i)).toFixed(dm)) + ' ' + sizes[i];
}
