#include "yt_sig_decipher.hpp"
#include <iostream>
#include <regex>
#include <sstream> // For URL decoding (basic)
#include <iomanip> // For std::setw, std::hex for URL decoding

// Basic URL decoding (very simplified, not for production without more robustness)
std::string url_decode(const std::string& encoded) {
    std::ostringstream decoded;
    decoded.fill('0');

    for (size_t i = 0; i < encoded.length(); ++i) {
        if (encoded[i] == '%') {
            if (i + 2 < encoded.length()) {
                std::string hex_val_str = encoded.substr(i + 1, 2);
                try {
                    int val = std::stoi(hex_val_str, nullptr, 16);
                    decoded << static_cast<char>(val);
                    i += 2;
                } catch (const std::invalid_argument& e) {
                    decoded << '%' << encoded[i+1] << encoded[i+2]; // Not a valid hex, pass through
                     i += 2;
                } catch (const std::out_of_range& e) {
                     decoded << '%' << encoded[i+1] << encoded[i+2]; // Not a valid hex, pass through
                     i += 2;
                }

            } else {
                decoded << encoded[i]; // Incomplete escape sequence
            }
        } else if (encoded[i] == '+') {
            decoded << ' ';
        } else {
            decoded << encoded[i];
        }
    }
    return decoded.str();
}


SignatureDecipherer::SignatureDecipherer() {
    ctx = duk_create_heap_default();
    if (!ctx) {
        std::cerr << "Failed to create Duktape heap." << std::endl;
    }
}

SignatureDecipherer::~SignatureDecipherer() {
    if (ctx) {
        duk_destroy_heap(ctx);
    }
}

// Simplified regex patterns - THESE ARE EXTREMELY FRAGILE AND LIKELY TO BREAK
// They are inspired by patterns yt-dlp might use but are not guaranteed to work.
// Actual yt-dlp patterns are more complex and numerous.

// Pattern to find a function like: function_name(a){a=a.split(""); ... ;return a.join("")}
// Or: var_name=function(a){a=a.split(""); ... ;return a.join("")}
// Or: key_name:function(a){a=a.split(""); ... ;return a.join("")}
const std::regex MAIN_DECIPHER_FUNC_REGEX[] = {
    std::regex(R"~(([a-zA-Z0-9$]{2,})\s*=\s*function\s*\(\s*a\s*\)\s*\{\s*a\s*=\s*a\.split\(\s*""\s*\);[^}]+\})~"),
    std::regex(R"~((?:function\s+([a-zA-Z0-9$]{2,})|([a-zA-Z0-9$]{2,})\s*:\s*function)\s*\(\s*a\s*\)\s*\{\s*a\s*=\s*a\.split\(\s*""\s*\);[^}]+\})~")
    // Add more patterns as needed
};

// Pattern to find the helper object name, e.g., if main func calls `HelperObj.method(a, ...)`
// This assumes the helper object is used with dot notation.
const std::regex HELPER_OBJ_NAME_REGEX(R"~([;,]\s*([a-zA-Z0-9$]{2,})\s*\.\s*[a-zA-Z0-9$]+\s*\()~");


std::string SignatureDecipherer::extract_main_decipher_fn_name(const std::string& script) {
    std::smatch match;
    for (const auto& re : MAIN_DECIPHER_FUNC_REGEX) {
        if (std::regex_search(script, match, re) && match.size() >= 2) {
            // Determine which capture group matched for the name
            // match[1] is usually the name if pattern is `name = function...` or `function name(...)`
            // match[2] could be for `name : function...`
            if (match[1].matched) return match[1].str();
            if (match.size() > 2 && match[2].matched) return match[2].str();
        }
    }
    std::cerr << "Could not find main decipher function name with current patterns." << std::endl;
    return "";
}

std::string SignatureDecipherer::extract_function_body(const std::string& script, const std::string& func_name) {
    if (func_name.empty()) return "";
    // Regex to find `func_name=function(...) { BODY }` or `func_name:function(...) { BODY }` or `function func_name(...) { BODY }`
    // This needs to be careful about nested braces. A simple regex is hard.
    // For simplicity, we'll grab from the first '{' to the matching '}'
    // This is a common simplification that might fail with complex JS.
    // The regex tries to match:
    // 1. `function func_name (...) { body }`
    // 2. `var|let|const func_name = function (...) { body }`
    // 3. `func_name : function (...) { body }` (within an object literal)
    // It captures the function parameters and the body.
    std::regex func_body_regex(
        "(?:function\\s+" + func_name + "\\s*\\(([^)]*)\\)|" + // Case 1: function func_name(params)
        "(?:var|const|let)\\s+" + func_name + "\\s*=\\s*function\\s*\\(([^)]*)\\)|" + // Case 2: var func_name = function(params)
        func_name + "\\s*:\\s*function\\s*\\(([^)]*)\\))" + // Case 3: func_name: function(params)
        "\\s*\\{([^}]*(?:\\{[^}]*\\}[^}]*)*)\\}" // Body with basic brace matching
    );

    std::smatch match;
    if (std::regex_search(script, match, func_body_regex)) {
        std::string params_str;
        std::string body_str;

        if (match[1].matched) { // Case 1 params
            params_str = match[1].str();
            body_str = match[4].str(); // Body is after all param groups
        } else if (match[2].matched) { // Case 2 params
            params_str = match[2].str();
            body_str = match[4].str();
        } else if (match[3].matched) { // Case 3 params
            params_str = match[3].str();
            body_str = match[4].str();
        } else {
             std::cerr << "Could not determine parameter list for function: " << func_name << std::endl;
            return "";
        }

        return "function " + func_name + "(" + params_str + ") {" + body_str + "}";
    }
    std::cerr << "Could not extract body for function: " << func_name << std::endl;
    return "";
}


std::string SignatureDecipherer::extract_helper_object_name(const std::string& main_func_body) {
    if (main_func_body.empty()) return "";
    std::smatch match;
    // Search within the main function's body for calls like `obj.method(...)`
    // This simplified regex looks for `variableName.methodName(`
    std::regex obj_call_regex(R"~\b([a-zA-Z0-9$_]{2,})\.([a-zA-Z0-9$_]{2,})\s*\(~");
    std::string::const_iterator search_start(main_func_body.cbegin());
    if (std::regex_search(search_start, main_func_body.cend(), match, obj_call_regex) && match.size() > 1) {
        return match[1].str(); // The object name
    }
    std::cerr << "Could not find helper object name in main decipher function body." << std::endl;
    return "";
}

std::string SignatureDecipherer::extract_object_definition(const std::string& script, const std::string& obj_name) {
    if (obj_name.empty()) return "";
    // Regex to find `var obj_name = { ... };` (also const or let)
    // This is highly simplified. Real helper objects can be complex.
    // It tries to match balanced curly braces.
    std::regex obj_def_regex("(?:var|const|let)\\s+" + obj_name + "\\s*=\\s*\\{([^}]*(?:\\{[^}]*\\}[^}]*)*)\\};");
    std::smatch match;
    if (std::regex_search(script, match, obj_def_regex) && match.size() > 1) {
        return "var " + obj_name + " = {" + match[1].str() + "};";
    }
    std::cerr << "Could not extract definition for object: " << obj_name << std::endl;
    return "";
}


bool SignatureDecipherer::initialize_operations(const std::string& player_script_content) {
    if (!ctx) return false;
    if (player_script_content.empty()) {
        std::cerr << "Player script content is empty." << std::endl;
        return false;
    }

    operations_.main_decipher_function_name = extract_main_decipher_fn_name(player_script_content);
    if (operations_.main_decipher_function_name.empty()) {
        std::cerr << "Failed to identify main decipher function name." << std::endl;
        return false;
    }
    std::cout << "Identified main decipher function name: " << operations_.main_decipher_function_name << std::endl;

    operations_.main_decipher_function_code = extract_function_body(player_script_content, operations_.main_decipher_function_name);
    if (operations_.main_decipher_function_code.empty()){
        std::cerr << "Failed to extract main decipher function body for: " << operations_.main_decipher_function_name << std::endl;
        return false;
    }
    std::cout << "Extracted main decipher function code (snippet): " << operations_.main_decipher_function_code.substr(0, std::min((size_t)100, operations_.main_decipher_function_code.length())) << "..." << std::endl;


    operations_.helper_object_name = extract_helper_object_name(operations_.main_decipher_function_code);
    if (operations_.helper_object_name.empty()) {
        std::cerr << "No helper object name found in main function. Assuming self-contained or global helpers." << std::endl;
        // It's possible the main function doesn't use a separate helper object in some player versions
        // or all logic is self-contained or uses global functions.
        // For now, we'll treat this as non-fatal if the main function is simple.
    } else {
        std::cout << "Identified helper object name: " << operations_.helper_object_name << std::endl;
        operations_.helper_object_code = extract_object_definition(player_script_content, operations_.helper_object_name);
        if (operations_.helper_object_code.empty()) {
            std::cerr << "Failed to extract helper object definition for: " << operations_.helper_object_name << ". Deciphering may fail if it's required." << std::endl;
            // This could be problematic if the main function relies on it.
        } else {
             std::cout << "Extracted helper object code (snippet): " << operations_.helper_object_code.substr(0, std::min((size_t)100, operations_.helper_object_code.length())) << "..." << std::endl;
        }
    }

    // At this point, we have names and potentially the code.
    // The next step (Execute Deciphering Logic) will use Duktape to run these.
    operations_.initialized = !operations_.main_decipher_function_code.empty();
    if(operations_.initialized){
        std::cout << "Decipher operations initialized successfully (based on regex extraction)." << std::endl;
    } else {
        std::cerr << "Decipher operations failed to initialize." << std::endl;
    }
    return operations_.initialized;
}

std::string SignatureDecipherer::decipher_signature(const std::string& encrypted_sig) {
    if (!ctx) {
        std::cerr << "Duktape context not available." << std::endl;
        return "";
    }
    if (!operations_.initialized || operations_.main_decipher_function_code.empty()) {
        std::cerr << "Decipher operations not initialized or main function code is missing." << std::endl;
        return "";
    }

    // std::cout << "Attempting to decipher signature using Duktape..." << std::endl;
    // std::cout << "Main function name: " << operations_.main_decipher_function_name << std::endl;
    // std::cout << "Encrypted signature: " << encrypted_sig << std::endl;

    // Load helper object if present
    if (!operations_.helper_object_code.empty()) {
        // std::cout << "Loading helper object code into Duktape..." << std::endl;
        if (duk_peval_string(ctx, operations_.helper_object_code.c_str()) != 0) {
            std::cerr << "Duktape Error (evaluating helper object): " << duk_safe_to_string(ctx, -1) << std::endl;
            duk_pop(ctx); // Pop error
            return "";
        }
        duk_pop(ctx); // Pop result of eval (undefined)
    }

    // Load main decipher function
    // std::cout << "Loading main decipher function code into Duktape..." << std::endl;
    if (duk_peval_string(ctx, operations_.main_decipher_function_code.c_str()) != 0) {
        std::cerr << "Duktape Error (evaluating main decipher function): " << duk_safe_to_string(ctx, -1) << std::endl;
        duk_pop(ctx); // Pop error
        return "";
    }
    duk_pop(ctx); // Pop result of eval (undefined)

    // Push the main decipher function onto the stack
    duk_push_global_object(ctx);
    duk_get_prop_string(ctx, -1, operations_.main_decipher_function_name.c_str());

    if (!duk_is_function(ctx, -1)) {
        std::cerr << "Duktape Error: '" << operations_.main_decipher_function_name << "' is not a function in JS context." << std::endl;
        duk_pop_n(ctx, 2); // Pop function and global object
        return "";
    }

    // Push the encrypted signature as an argument for the JS function
    duk_push_string(ctx, encrypted_sig.c_str());

    // Call the JS function (1 argument, 1 return value expected)
    // std::cout << "Calling JS decipher function '" << operations_.main_decipher_function_name << "' with '" << encrypted_sig << "'" << std::endl;
    if (duk_pcall(ctx, 1) != DUK_EXEC_SUCCESS) { // 1 argument pushed
        std::cerr << "Duktape Error (calling " << operations_.main_decipher_function_name << "): " << duk_safe_to_string(ctx, -1) << std::endl;
        duk_pop_n(ctx, 2); // Pop error and global object (or function if it was left there)
        return "";
    }

    std::string deciphered_signature;
    if (duk_is_string(ctx, -1)) {
        deciphered_signature = duk_safe_to_string(ctx, -1);
        // std::cout << "Successfully deciphered signature: " << deciphered_signature << std::endl;
    } else {
        std::cerr << "Duktape Error: JS decipher function did not return a string." << std::endl;
         // Optional: print what it did return
        // duk_json_encode(ctx, -1);
        // std::cerr << "Returned: " << duk_safe_to_string(ctx, -1) << std::endl;
        // duk_pop(ctx); // pop JSON encoded string
    }

    duk_pop_n(ctx, 2); // Pop result and global object (or function + result if global was already popped)
    return deciphered_signature;
}

bool SignatureDecipherer::parse_signature_cipher(const std::string& cipher_str,
                                               std::string& out_url,
                                               std::string& out_s,
                                               std::string& out_sp) {
    // Example cipher_str: "s=ABC&sp=sig&url=https://..."
    // or "url=https://...&s=ABC&sp=sig"
    // This is a simplified parser. A robust one would handle all edge cases.
    out_url.clear();
    out_s.clear();
    out_sp.clear();

    std::map<std::string, std::string> params;
    std::string current_key;
    std::string current_val;
    bool parsing_key = true;

    for (size_t i = 0; i < cipher_str.length(); ++i) {
        char c = cipher_str[i];
        if (c == '=') {
            if (parsing_key) parsing_key = false;
            else current_val += c; // '=' can be part of value
        } else if (c == '&') {
            if (!current_key.empty()) {
                params[url_decode(current_key)] = url_decode(current_val);
            }
            current_key.clear();
            current_val.clear();
            parsing_key = true;
        } else {
            if (parsing_key) {
                current_key += c;
            } else {
                current_val += c;
            }
        }
    }
    // Add the last parameter
    if (!current_key.empty()) {
        params[url_decode(current_key)] = url_decode(current_val);
    }

    if (params.count("url") && params.count("s")) {
        out_url = params["url"];
        out_s = params["s"];
        out_sp = params.count("sp") ? params["sp"] : "signature"; // Default to "signature" if sp is missing
        if (out_sp.empty()) out_sp = "signature"; // Ensure sp is not empty
        return true;
    }
    std::cerr << "Failed to parse signature cipher string: '" << cipher_str << "'. Missing 'url' or 's' parameter." << std::endl;
    std::cerr << "  Parsed params:" << std::endl;
    for(const auto& pair : params){
        std::cerr << "    '" << pair.first << "': '" << pair.second << "'" << std::endl;
    }
    return false;
}
