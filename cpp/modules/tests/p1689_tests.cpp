#include <iostream>
#include <string>
#include <string_view>

#include "mqb/modules/P1689.hpp"

namespace {

int failures = 0;

void expect(const bool condition, const std::string_view message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

} // namespace

int main() {
    using mqb::modules::LookupMethod;
    using mqb::modules::P1689ErrorCode;
    using mqb::modules::P1689Parser;

    {
        auto document = P1689Parser::parse(R"json({
  "version": 1,
  "revision": 0,
  "rules": [
    {
      "primary-output": "duplicate.obj",
      "provides": [
        { "logical-name": "duplicate" }
      ]
    },
    {
      "primary-output": "another.obj",
      "outputs": ["another.module.json"],
      "provides": [
        {
          "logical-name": "another",
          "compiled-module-path": "ifc/another.ifc",
          "is-interface": false
        }
      ],
      "requires": [
        { "logical-name": "duplicate" },
        {
          "logical-name": "header.hpp",
          "source-path": "C:\\project\\header.hpp",
          "unique-on-source-path": true,
          "lookup-method": "include-quote"
        }
      ],
      "_MSVC_future": { "ignored": true }
    }
  ],
  "_VENDOR_document": 1
})json");

        expect(document.has_value(), "valid P1689R5 document should parse");
        if (document) {
            expect(document->version == 1 && document->revision == 0,
                   "version and revision should be preserved");
            expect(document->rules.size() == 2,
                   "all P1689 rules should be preserved");
            expect(document->rules[0].provided_modules.size() == 1
                       && document->rules[0].provided_modules[0].logical_name == "duplicate",
                   "provided named module should decode");
            expect(document->rules[0].provided_modules[0].is_interface,
                   "provided module should default is-interface to true");
            expect(document->rules[1].provided_modules[0].logical_name == "another"
                       && !document->rules[1].provided_modules[0].is_interface,
                   "explicit is-interface=false should decode");
            expect(document->rules[1].required_modules[0].lookup_method == LookupMethod::by_name,
                   "required named module should default lookup-method to by-name");
            expect(document->rules[1].required_modules[1].lookup_method == LookupMethod::include_quote
                       && document->rules[1].required_modules[1].unique_on_source_path
                       && document->rules[1].required_modules[1].source_path.has_value(),
                   "header-unit lookup identity should decode");
        }
    }

    {
        auto no_revision = P1689Parser::parse(R"json({
  "version": 1,
  "rules": [ { "requires": [ { "logical-name": "std" } ] } ]
})json");
        expect(no_revision.has_value() && no_revision->revision == 0,
               "omitted revision should default to zero");
    }

    {
        auto unknown = P1689Parser::parse(R"json({
  "version": 1,
  "rules": [ { "mystery": true } ]
})json");
        expect(!unknown && unknown.error().code == P1689ErrorCode::schema_error,
               "unknown non-vendor fields should be rejected");
    }

    {
        auto missing_name = P1689Parser::parse(R"json({
  "version": 1,
  "rules": [ { "provides": [ { "is-interface": true } ] } ]
})json");
        expect(!missing_name && missing_name.error().code == P1689ErrorCode::schema_error,
               "module descriptions must require logical-name");
    }

    {
        auto missing_source = P1689Parser::parse(R"json({
  "version": 1,
  "rules": [ {
    "requires": [ {
      "logical-name": "header.hpp",
      "unique-on-source-path": true
    } ]
  } ]
})json");
        expect(!missing_source && missing_source.error().code == P1689ErrorCode::schema_error,
               "source-unique module identities must provide source-path");
    }

    {
        auto bad_lookup = P1689Parser::parse(R"json({
  "version": 1,
  "rules": [ { "requires": [ {
    "logical-name": "M",
    "lookup-method": "magic"
  } ] } ]
})json");
        expect(!bad_lookup && bad_lookup.error().code == P1689ErrorCode::schema_error,
               "unknown lookup methods should be rejected");
    }

    {
        auto bad_version = P1689Parser::parse(R"json({"version":2,"rules":[{}]})json");
        expect(!bad_version && bad_version.error().code == P1689ErrorCode::unsupported_version,
               "future semantic versions should fail explicitly");
    }

    {
        auto bad_revision = P1689Parser::parse(R"json({"version":1,"revision":1,"rules":[{}]})json");
        expect(!bad_revision && bad_revision.error().code == P1689ErrorCode::unsupported_revision,
               "unsupported revisions should fail explicitly");
    }

    {
        auto empty_rules = P1689Parser::parse(R"json({"version":1,"rules":[]})json");
        expect(!empty_rules && empty_rules.error().code == P1689ErrorCode::schema_error,
               "P1689 rules must be non-empty");
    }

    {
        auto malformed = P1689Parser::parse("{\"version\":1,]");
        expect(!malformed && malformed.error().code == P1689ErrorCode::parse_error,
               "malformed JSON should remain a parse error");
        if (!malformed) {
            expect(malformed.error().line >= 1 && malformed.error().column >= 1,
                   "P1689 parse errors should retain JSON coordinates");
        }
    }

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }

    std::cout << "mqb_p1689_tests passed\n";
    return 0;
}
