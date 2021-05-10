#include <vector>
#include <map>
#include <queue>
#include <stack>
#include <cstring>
#include <cstdlib>
#include "Modules/Engine.hpp"
#include "Modules/Server.hpp"
#include "Features/Session.hpp"
#include "Event.hpp"

struct Condition {
    enum {
        ORANGE,
        COOP,
        CM,
        SAME_MAP,
        MAP,
        PREV_MAP,
        NOT,
        AND,
        OR,
    } type;

    union {
        char *map;
        Condition *unop_cond;
        struct {
            Condition *binop_l, *binop_r;
        };
    };
};

static void FreeCondition(Condition *c) {
    switch (c->type) {
    case Condition::MAP:
    case Condition::PREV_MAP:
        free(c->map);
        break;
    case Condition::NOT:
        FreeCondition(c->unop_cond);
        break;
    case Condition::AND:
    case Condition::OR:
        FreeCondition(c->binop_l);
        FreeCondition(c->binop_r);
        break;
    default:
        break;
    }

    free(c);
}

static bool EvalCondition(Condition *c) {
    switch (c->type) {
    case Condition::ORANGE: return engine->IsOrange();
    case Condition::COOP: return engine->IsCoop();
    case Condition::CM: return server->GetChallengeStatus() == CMStatus::CHALLENGE;
    case Condition::SAME_MAP: return session->previousMap == engine->GetCurrentMapName();
    case Condition::MAP: return !strcmp(c->map, engine->GetCurrentMapName().c_str());
    case Condition::PREV_MAP: return !strcmp(c->map, session->previousMap.c_str());
    case Condition::NOT: return !EvalCondition(c->unop_cond);
    case Condition::AND: return EvalCondition(c->binop_l) && EvalCondition(c->binop_r);
    case Condition::OR: return EvalCondition(c->binop_l) || EvalCondition(c->binop_r);
    }
    return false;
}

// Parsing {{{

enum TokenType {
    TOK_LPAREN,
    TOK_RPAREN,
    TOK_NOT,
    TOK_AND,
    TOK_OR,
    TOK_EQUALS,
    TOK_STR,
};

struct Token {
    enum TokenType type;
    // These are for TOK_STR
    const char *str;
    size_t len;
};

static bool _isIdentChar(char c) {
    return
        c != ' ' &&
        c != '\v' &&
        c != '\t' &&
        c != '\r' &&
        c != '\n' &&
        c != '(' &&
        c != ')' &&
        c != '!' &&
        c != '&' &&
        c != '|' &&
        c != '=';
}

static std::queue<Token> LexCondition(const char *str, size_t len) {
    std::queue<Token> toks;

    const char *end = str + len;

    while (str < end) {
        switch (*str) {
        case ' ':
        case '\v':
        case '\t':
        case '\r':
        case '\n':
            break;
        case '(':
            toks.push({TOK_LPAREN});
            break;
        case ')':
            toks.push({TOK_RPAREN});
            break;
        case '!':
            toks.push({TOK_NOT});
            break;
        case '&':
            toks.push({TOK_AND});
            break;
        case '|':
            toks.push({TOK_OR});
            break;
        case '=':
            toks.push({TOK_EQUALS});
            break;
        default:
            const char *start = str;
            while (str < end && _isIdentChar(*str)) {
                ++str;
            }
            size_t len = str - start;
            toks.push({TOK_STR, start, len});
            continue;
        }
        ++str;
    }

    return toks;
}

static Condition *ParseCondition(std::queue<Token> toks) {
    std::stack<enum TokenType> op_stack;
    std::stack<Condition *> out_stack;

#define CLEAR_OUT_STACK do { while (!out_stack.empty()) { FreeCondition(out_stack.top()); out_stack.pop(); } } while (0)

#define POP_OP_TO_OUTPUT do { \
    enum TokenType op = op_stack.top(); \
    op_stack.pop(); \
    if (out_stack.empty()) { \
        console->Print("Malformed input\n"); \
        CLEAR_OUT_STACK; \
        return NULL; \
    } \
    Condition *c_new = (Condition *)malloc(sizeof *c_new);\
    if (op == TOK_NOT) { \
        c_new->type = Condition::NOT; \
        c_new->unop_cond = out_stack.top(); \
        out_stack.pop(); \
    } else { \
        c_new->type = op == TOK_OR ? Condition::OR : Condition::AND; \
        c_new->binop_r = out_stack.top(); \
        out_stack.pop(); \
        if (out_stack.empty()) { \
            console->Print("Malformed input\n"); \
            CLEAR_OUT_STACK; \
            return NULL; \
        } \
        c_new->binop_l = out_stack.top(); \
        out_stack.pop(); \
    } \
    out_stack.push(c_new); \
} while (0)

    while (!toks.empty()) {
        Token t = toks.front();
        toks.pop();

        switch (t.type) {
        // TOK_STR {{{
        case TOK_STR: {
            Condition *c = (Condition *)malloc(sizeof *c);

            if (!strncmp(t.str, "orange", t.len)) {
                c->type = Condition::ORANGE;
            } else if (t.len == 4 && !strncmp(t.str, "coop", t.len)) {
                c->type = Condition::COOP;
            } else if (t.len == 2 && !strncmp(t.str, "cm", t.len)) {
                c->type = Condition::CM;
            } else if (t.len == 8 && !strncmp(t.str, "same_map", t.len)) {
                c->type = Condition::SAME_MAP;
            } else if (t.len == 3 && !strncmp(t.str, "map", t.len) || t.len == 8 && !strncmp(t.str, "prev_map", t.len)) {
                bool is_prev_map = t.len == 8;

                if (toks.front().type != TOK_EQUALS) {
                    if (is_prev_map) console->Print("Expected = after 'prev_map'\n");
                    else console->Print("Expected = after 'map'\n");
                    CLEAR_OUT_STACK;
                    return NULL;
                }

                toks.pop();

                Token map_tok = toks.front();
                toks.pop();

                if (map_tok.type != TOK_STR) {
                    if (is_prev_map) console->Print("Expected string token after 'prev_map='\n");
                    else console->Print("Expected string token after 'map='\n");
                    CLEAR_OUT_STACK;
                    return NULL;
                }

                c->type = is_prev_map ? Condition::PREV_MAP : Condition::MAP;
                c->map = (char *)malloc(map_tok.len + 1);
                strncpy(c->map, map_tok.str, map_tok.len);
                c->map[map_tok.len] = 0; // Null terminator
            } else {
                console->Print("Bad token '%.*s'\n", t.len, t.str);
                CLEAR_OUT_STACK;
                return NULL;
            }

            out_stack.push(c);

            break;
        }
        // }}}

        // TOK_LPAREN / TOK_NOT {{{
        case TOK_LPAREN:
        case TOK_NOT: {
            op_stack.push(t.type);
            break;
        }
        // }}}

            // TOK_RPAREN {{{
            case TOK_RPAREN:
                while (!op_stack.empty() && op_stack.top() != TOK_LPAREN) {
                    POP_OP_TO_OUTPUT;
                }

                if (op_stack.empty()) {
                    console->Print("Unmatched parentheses\n");
                    CLEAR_OUT_STACK;
                    return NULL;
                }

                op_stack.pop();

                break;
            // }}}

        // TOK_AND / TOK_OR {{{
        case TOK_AND:
        case TOK_OR: {
            while (!op_stack.empty() && (op_stack.top() == TOK_NOT || op_stack.top() == TOK_AND)) {
                POP_OP_TO_OUTPUT;
            }
            op_stack.push(t.type);
            break;
        }
        // }}}

        // TOK_EQUALS {{{
        case TOK_EQUALS: {
            console->Print("Unexpected '=' token\n");
            CLEAR_OUT_STACK;
            return NULL;
        }
        // }}}
        }
    }

    while (!op_stack.empty()) {
        POP_OP_TO_OUTPUT;
    }

#undef POP_OP_TO_OUTPUT
#undef CLEAR_OUT_STACK

    if (out_stack.empty()) {
        console->Print("Malformed input\n");
        return NULL;
    }

    Condition *cond = out_stack.top();
    out_stack.pop();

    if (!out_stack.empty()) {
        console->Print("Malformed input\n");
        FreeCondition(cond);
        return NULL;
    }

    return cond;
}

// }}}

#define MK_SAR_ON(name, when, immediately) \
    static std::vector<std::string> _g_execs_##name; \
    CON_COMMAND(sar_on_##name, "sar_on_" #name " <command> [args]... - registers a command to be run " when ".\n") { \
        if (args.ArgC() < 2) { \
            return console->Print(sar_on_##name.ThisPtr()->m_pszHelpString); \
        } \
        _g_execs_##name.push_back(std::string(args.m_pArgSBuffer + args.m_nArgv0Size)); \
    } \
    static void _runExecs_##name() { \
        for (auto cmd : _g_execs_##name) { \
            engine->ExecuteCommand(cmd.c_str(), immediately); \
        } \
    }

#define RUN_EXECS(x) _runExecs_##x()

MK_SAR_ON(load, "on level load", true)
MK_SAR_ON(exit, "on game exit", false)
MK_SAR_ON(demo_start, "when demo playback starts", false)
MK_SAR_ON(demo_stop, "when demo playback stops", false)

struct Seq {
    std::queue<std::string> commands;
};

std::vector<Seq> seqs;

CON_COMMAND(cond, "cond [condition] [command] [args]... - runs a command only if a given condition is met.\n")
{
    if (args.ArgC() < 3) {
        return console->Print(cond.ThisPtr()->m_pszHelpString);
    }

    const char *cond_str = args[1];

    Condition *cond = ParseCondition(LexCondition(cond_str, strlen(cond_str)));

    if (!cond) {
        console->Print("Condition parsing failed\n");
        return;
    }

    if (EvalCondition(cond)) {
        std::string cmd = args[2];
        for (int i = 3; i < args.ArgC(); ++i) {
            cmd += Utils::ssprintf(" \"%s\"", args[i]);
        }
        engine->ExecuteCommand(cmd.c_str());
    }
}

CON_COMMAND(seq, "seq [command]... - runs a sequence of commands one tick after one another.\n")
{
    if (args.ArgC() < 2) {
        return console->Print(seq.ThisPtr()->m_pszHelpString);
    }

    std::queue<std::string> cmds;
    for (int i = 1; i < args.ArgC(); ++i) {
        cmds.push(std::string(args[i]));
    }

    seqs.push_back({cmds});
}

ON_EVENT(TICK) {
    for (size_t i = 0; i < seqs.size(); ++i) {
        if (seqs[i].commands.empty()) {
            seqs.erase(seqs.begin() + i);
            i--; // Decrement the index to account for the removed element
            continue;
        }

        std::string cmd = seqs[i].commands.front();
        seqs[i].commands.pop();

        engine->ExecuteCommand(cmd.c_str(), true);
    }
}

static std::map<std::string, std::string> g_aliases;

CON_COMMAND(sar_alias_run, "sar_alias_run <name> [args]... - run a SAR alias, passing on any additional arguments.\n")
{
    if (args.ArgC() < 2) {
        return console->Print(sar_alias_run.ThisPtr()->m_pszHelpString);
    }

    std::string cmd = g_aliases[std::string(args[1])];
    for (int i = 2; i < args.ArgC(); ++i) {
        cmd += Utils::ssprintf(" \"%s\"", args[i]);
    }

    engine->ExecuteCommand(cmd.c_str(), true);
}

CON_COMMAND(sar_alias, "sar_alias <name> [command] [args]... - create an alias, similar to the 'alias' command but not requiring quoting. If no command is specified, prints the given alias.\n")
{
    if (args.ArgC() < 2) {
        return console->Print(sar_alias.ThisPtr()->m_pszHelpString);
    }

    if (args.ArgC() == 2) {
        auto alias = g_aliases.find({args[1]});
        if (alias == g_aliases.end()) {
            console->Print("Alias %s does not exist!\n", args[1]);
        } else {
            console->Print("%s\n", alias->second.c_str());
        }
        return;
    }

    std::string cmd = args[2];
    for (int i = 3; i < args.ArgC(); ++i) {
        cmd += Utils::ssprintf(" \"%s\"", args[i]);
    }

    engine->ExecuteCommand(Utils::ssprintf("alias \"%s\" sar_alias_run \"%s\"", args[1], args[1]).c_str());
    g_aliases[std::string(args[1])] = cmd;
}

ON_EVENT_P(SESSION_START, 1000000) { RUN_EXECS(load); }
ON_EVENT(SAR_UNLOAD) { RUN_EXECS(exit); }
ON_EVENT(DEMO_START) { RUN_EXECS(demo_start); }
ON_EVENT(DEMO_STOP) { RUN_EXECS(demo_stop); }
