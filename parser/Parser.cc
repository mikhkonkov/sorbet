#include "parser.h"
#include "Builder.h"
#include "core/Loc.h"
#include "core/errors/parser.h"
#include "ruby_parser/driver.hh"
#include <algorithm>

template class std::unique_ptr<sorbet::parser::Node>;

using namespace std;

namespace sorbet::parser {

extern const char *dclassStrings[];

using namespace std;

class ErrorToError {
    static u4 translatePos(size_t pos, u4 maxOff) {
        if (pos == 0) {
            return pos;
        }
        return min((u4)(pos), maxOff);
    }

public:
    static void run(core::GlobalState &gs, core::FileRef file, ruby_parser::diagnostics_t diagnostics) {
        if (diagnostics.empty()) {
            return;
        }
        u4 maxOff = file.data(gs).source().size();
        file.data(gs).hasParseErrors = true;
        for (auto &diag : diagnostics) {
            switch (diag.level()) {
                case ruby_parser::dlevel::NOTE:
                case ruby_parser::dlevel::WARNING:
                    continue;
                case ruby_parser::dlevel::ERROR:
                case ruby_parser::dlevel::FATAL:
                    break;
            }
            core::Loc loc(file, translatePos(diag.location().beginPos, maxOff - 1),
                          translatePos(diag.location().endPos, maxOff));

            auto sorbetErrorClass = diag.error_class() == ruby_parser::dclass::MethodWithoutSelector
                                        ? core::errors::Parser::MethodWithoutSelector
                                        : core::errors::Parser::ParserError;
            if (auto e = gs.beginError(loc, sorbetErrorClass)) {
                e.setHeader("{}", fmt::format(dclassStrings[(int)diag.error_class()], diag.data()));
            }
        }
    }
};

unique_ptr<Node> Parser::run(sorbet::core::GlobalState &gs, core::FileRef file,
                             std::vector<std::string> initialLocals) {
    Builder builder(gs, file);
    auto source = file.data(gs).source();
    ruby_parser::typedruby25 driver(string(source.begin(), source.end()), Builder::interface);

    for (string local : initialLocals) {
        driver.lex.declare(local);
    }

    auto ast = unique_ptr<Node>(builder.build(&driver));
    ErrorToError::run(gs, file, driver.diagnostics);

    if (!ast) {
        core::Loc loc(file, 0, 0);
        NodeVec empty;
        return make_unique<Begin>(loc, std::move(empty));
    }

    return ast;
}

unique_ptr<Node> Parser::run(sorbet::core::GlobalState &gs, string_view path, string_view src,
                             std::vector<std::string> initialLocals) {
    core::FileRef file = gs.enterFile(path, src);
    return run(gs, file, initialLocals);
}

}; // namespace sorbet::parser
