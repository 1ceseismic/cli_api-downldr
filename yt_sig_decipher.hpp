#ifndef YT_SIG_DECIPHER_HPP
#define YT_SIG_DECIPHER_HPP

#include <string>
#include <vector>
#include <map>
// Note: duktape.h should be in the include path during compilation
#include "duktape.h" // Assuming duktape.h is available

// Structure to hold the necessary components for deciphering
struct DecipherOperations {
    std::string main_decipher_function_name;
    std::string main_decipher_function_code; // The JS code for the main function
    std::string helper_object_name;         // Name of the helper object (e.g., "Bo")
    std::string helper_object_code;         // The JS code for the helper object definition
    bool initialized = false;
};

class SignatureDecipherer {
public:
    SignatureDecipherer();
    ~SignatureDecipherer();

    // Attempts to find and extract decipher functions from the player script
    bool initialize_operations(const std::string& player_script_content);

    // Deciphers a signature string using the loaded operations
    std::string decipher_signature(const std::string& encrypted_sig);

    // Parses the "signatureCipher" string (e.g., "s=...&url=...")
    static bool parse_signature_cipher(const std::string& cipher_str,
                                       std::string& out_url,
                                       std::string& out_s, // encrypted signature
                                       std::string& out_sp); // signature parameter name

private:
    duk_context* ctx = nullptr; // Duktape context
    DecipherOperations operations_;

    // Regex-based extraction (highly fragile, for demonstration)
    std::string extract_main_decipher_fn_name(const std::string& script);
    std::string extract_function_body(const std::string& script, const std::string& func_name);
    std::string extract_helper_object_name(const std::string& main_func_body);
    std::string extract_object_definition(const std::string& script, const std::string& obj_name_pattern);
};

#endif // YT_SIG_DECIPHER_HPP
