#pragma once
#include "../Typechecker.hpp"
#include "../../Lexer/Lexer.hpp"
#include "../../Parser/Parser.hpp"
#include "../../utils/utils.hpp"

node_ptr Typechecker::tc_function(node_ptr& node) {
    // Check if the body is a type, if so, this function is a type

    if (!node->_Node.Function().body) {
        // If 'Function'
        node->_Node.Function().body = new_node(NodeType::ANY);
    }

    if (node->_Node.Function().body->type == NodeType::ID) {

        // We want to check if this ID is a type, in this case we mark
        // this function as a type

        auto value = get_symbol(node->_Node.Function().body->_Node.ID().value, current_scope);
        if (value && value->TypeInfo.is_type) {
            node->TypeInfo.is_type = true;
        }
    }

    if (node->_Node.Function().body->TypeInfo.is_type) {
        node->_Node.Function().return_type = node->_Node.Function().body;
        node->TypeInfo.is_type = true;
    }

    node_ptr function = copy_function(node);

    auto local_scope = std::make_shared<SymbolTable>();
    auto curr_scope = std::make_shared<SymbolTable>();

    current_scope->child = local_scope;
    local_scope->parent = current_scope;

    local_scope->child = curr_scope;
    curr_scope->parent = local_scope;

    // current_scope 
    //    -> local_scope 
    //        -> curr_scope

    current_scope = curr_scope;
    current_scope->file_name = function->_Node.Function().name + ": " + function->_Node.Function().decl_filename;
    
    // Inject closure into local scope

    for (auto& elem : function->_Node.Function().closure) {
        add_symbol(elem.first, elem.second, local_scope);
    }

    for (int i = 0; i < function->_Node.Function().params.size(); i++) {
        node_ptr param_type;
        std::string name = function->_Node.Function().params[i]->_Node.ID().value;
        if (function->_Node.Function().param_types[name]) {
            param_type = tc_node(function->_Node.Function().param_types[name]);
            function->_Node.Function().param_types[name] = param_type;

            // If it's a refinement type, we inject first param if known, else Any
            if (function->_Node.Function().param_types[name]->type == NodeType::FUNC && function->_Node.Function().param_types[name]->TypeInfo.is_refinement_type) {
                node_ptr& ref_type = function->_Node.Function().param_types[name];
                std::string param_name = ref_type->_Node.Function().params[0]->_Node.ID().value;
                if (ref_type->_Node.Function().param_types[param_name]) {
                    param_type = tc_node(ref_type->_Node.Function().param_types[param_name]);
                } else {
                    param_type = new_node(NodeType::ANY);
                }
            }
        } else {
            param_type = new_node(NodeType::ANY);
            function->_Node.Function().param_types[name] = param_type;

        }
        add_symbol(name, param_type, current_scope);
    }

    node_ptr res = new_node(NodeType::NONE);

    if (function->_Node.Function().body->type != NodeType::OBJECT) {
        node_ptr evaluated_res = tc_node(function->_Node.Function().body);
        if (evaluated_res->type == NodeType::NOVALUE) {
            res = new_node(NodeType::NONE);
        } else {
            res = evaluated_res;
        }

        if (res->type == NodeType::PIPE_LIST) {
            res->_Node.List().elements.erase(std::unique(res->_Node.List().elements.begin(), res->_Node.List().elements.end(), [this](node_ptr& lhs, node_ptr& rhs) { return compareNodeTypes(lhs, rhs); }), res->_Node.List().elements.end());

            if (res->_Node.List().elements.size() == 1) {
                res = res->_Node.List().elements[0];
            }
        }

    } else {
        std::vector<node_ptr> return_types;
        for (int i = 0; i < function->_Node.Function().body->_Node.Object().elements.size(); i++) {
            node_ptr expr = function->_Node.Function().body->_Node.Object().elements[i];
            node_ptr evaluated_expr = tc_node(expr);
            // Check if we've evaluated an if statement
            // If we have and we have no returns AND this is the last expression
            // Append 'None'
            if (expr->type == NodeType::IF_STATEMENT && evaluated_expr->type == NodeType::NOVALUE && i == function->_Node.Function().body->_Node.Object().elements.size()-1) {
                return_types.push_back(new_node(NodeType::NONE));
            }
            // Check if we've evaluated an if block
            // If we have and it's the last expression AND the number of returns does not match the number of statements
            // Append 'None'
            else if (expr->type == NodeType::IF_BLOCK && (evaluated_expr->type == NodeType::PIPE_LIST) && i == function->_Node.Function().body->_Node.Object().elements.size()-1) {
                if (evaluated_expr->_Node.List().elements.size() != expr->_Node.IfBlock().statements.size()) {
                    return_types.push_back(new_node(NodeType::NONE));
                }
            }

            // TODO: Nested branching doesn't really get captured - not good.

            if (evaluated_expr->type == NodeType::ERROR) {
                return throw_error(evaluated_expr->_Node.Error().message);
            }
            else if (evaluated_expr->type == NodeType::NOVALUE) {
                continue;
            }
            else if (evaluated_expr->type == NodeType::PIPE_LIST && !evaluated_expr->Meta.literal_construct && evaluated_expr->_Node.List().elements.size() > 0) {
                for (node_ptr& elem : evaluated_expr->_Node.List().elements) {
                    if (elem->type == NodeType::RETURN) {
                        if (elem->_Node.Return().value == nullptr) {
                            return_types.push_back(new_node(NodeType::NONE));
                        } else {
                            node_ptr evaluated_value = tc_node(elem->_Node.Return().value);
                            return_types.push_back(evaluated_value);
                        }
                    }
                }
            }
            else if (evaluated_expr->type == NodeType::RETURN) {
                if (evaluated_expr->_Node.Return().value == nullptr) {
                    return_types.push_back(new_node(NodeType::NONE));
                } else {
                    node_ptr evaluated_value = tc_node(evaluated_expr->_Node.Return().value);
                    return_types.push_back(evaluated_value);
                }
            } 
            
            if (i == function->_Node.Function().body->_Node.Object().elements.size()-1) {
                if (evaluated_expr->type == NodeType::RETURN) {
                    if (evaluated_expr->_Node.Return().value == nullptr) {
                        return_types.push_back(new_node(NodeType::NONE));
                    } else {
                        return_types.push_back(tc_node(evaluated_expr->_Node.Return().value));
                    }
                } else {
                    return_types.push_back(evaluated_expr);
                }
            }
        }

        std::vector<node_ptr> set;

        for (node_ptr& t : return_types) {
            if (t->type == NodeType::PIPE_LIST) {
                for (node_ptr& e : t->_Node.List().elements) {
                    set.push_back(e);
                }
            } else {
                set.push_back(t);
            }
        }

        set = sort_and_unique(set);

        return_types = set;

        if (return_types.size() == 1) {
            res = return_types[0];
        } else if (return_types.size() > 1) {                 
            res = new_node(NodeType::LIST);
            res->type = NodeType::PIPE_LIST;
            for (node_ptr& elem : return_types) {
                node_ptr r = std::make_shared<Node>(*elem);
                r->TypeInfo.is_type = true;
                res->_Node.List().elements.push_back(r);
            }
            res = get_type(res);
        }
    }

    if (!function->_Node.Function().type_function) {
        // Check against return type
        if (function->_Node.Function().return_type) {
            function->_Node.Function().return_type = tc_node(function->_Node.Function().return_type);
            if (!match_types(function->_Node.Function().return_type, res)) {
                node_ptr defined_ret_type = get_type(function->_Node.Function().return_type);
                node_ptr ret_type = get_type(res);
                return throw_error("Type Error in '" + function->_Node.Function().name + "': Return type '" + printable(ret_type) + "' does not match defined return type '" + printable(defined_ret_type) + "'");
            }
        } else {
            function->_Node.Function().return_type = res;
        }
    } else {
        function->_Node.Function().return_type = res;
    }

    if (function->_Node.Function().return_type->type == NodeType::FUNC) {
        function->_Node.Function().return_type = tc_function(function->_Node.Function().return_type);
    } else {
        function->_Node.Function().return_type->TypeInfo.is_type = true;
    }

    current_scope = current_scope->parent->parent;
    current_scope->child->child = nullptr;
    current_scope->child = nullptr;

    function->Meta.typechecked = true;

    // Testing: change the node's return_type here so that the evaluator will have
    // a fully typed function

    node->_Node.Function().return_type = std::make_shared<Node>(*function->_Node.Function().return_type);

    return function;
}

node_ptr Typechecker::tc_func_call(node_ptr& node, node_ptr func = nullptr) {

    node_ptr function = new_node(NodeType::FUNC);

    if (node->_Node.FunctionCall().inline_func) {
        node_ptr inline_func_call = new_node(NodeType::FUNC_CALL);
        inline_func_call->_Node.FunctionCall().args = node->_Node.FunctionCall().args;
        return tc_func_call(inline_func_call, tc_node(node->_Node.FunctionCall().inline_func));
    }

    if (node->_Node.FunctionCall().name == "typeof") {

        int num_args = 1;

        if (node->_Node.FunctionCall().args.size() != num_args) {
            return throw_error("Function '" + node->_Node.FunctionCall().name + "' expects " + std::to_string(num_args) + " argument(s)");
        }

        node_ptr arg = tc_node(node->_Node.FunctionCall().args[0]);
        return get_type(arg);
    }

    if (node->_Node.FunctionCall().name == "print") {
        if (node->_Node.FunctionCall().args.size() == 1) {
            node_ptr evaluated_arg = tc_node(node->_Node.FunctionCall().args[0]);
            if (evaluated_arg->type == NodeType::ERROR) {
                return throw_error(evaluated_arg->_Node.Error().message);
            }
        } else {
            for (node_ptr arg : node->_Node.FunctionCall().args) {
                node_ptr evaluated_arg = tc_node(arg);
                if (evaluated_arg->type == NodeType::ERROR) {
                return throw_error(evaluated_arg->_Node.Error().message);
            }
            }
        }

        return new_node(NodeType::NONE);
    }

    if (node->_Node.FunctionCall().name == "println") {
        if (node->_Node.FunctionCall().args.size() == 1) {
            node_ptr evaluated_arg = tc_node(node->_Node.FunctionCall().args[0]);
            if (evaluated_arg->type == NodeType::ERROR) {
                return throw_error(evaluated_arg->_Node.Error().message);
            }
        } else {
            for (node_ptr arg : node->_Node.FunctionCall().args) {
                node_ptr evaluated_arg = tc_node(arg);
                if (evaluated_arg->type == NodeType::ERROR) {
                return throw_error(evaluated_arg->_Node.Error().message);
            }
            }
        }

        return new_node(NodeType::NONE);
    }

    if (node->_Node.FunctionCall().name == "refcount") {

        int num_args = 1;

        if (node->_Node.FunctionCall().args.size() != num_args) {
            return throw_error("Function '" + node->_Node.FunctionCall().name + "' expects " + std::to_string(num_args) + " argument(s)");
        }

        return new_number_node(0);
    }

    if (node->_Node.FunctionCall().name == "error") {

        int num_args = 1;

        if (node->_Node.FunctionCall().args.size() != num_args) {
            return throw_error("Function '" + node->_Node.FunctionCall().name + "' expects " + std::to_string(num_args) + " argument(s)");
        }

        node_ptr arg = tc_node(node->_Node.FunctionCall().args[0]);
        return new_node(NodeType::NOVALUE);
    }

    if (node->_Node.FunctionCall().name == "string") {
        int num_args = 1;

        if (node->_Node.FunctionCall().args.size() != num_args) {
            return throw_error("Function '" + node->_Node.FunctionCall().name + "' expects " + std::to_string(num_args) + " argument(s)");
        }

        node_ptr arg = tc_node(node->_Node.FunctionCall().args[0]);
        return new_string_node(printable(arg));
    }

    if (node->_Node.FunctionCall().name == "number") {

        int num_args = 2;

        if (node->_Node.FunctionCall().args.size() > num_args) {
            return throw_error("Function '" + node->_Node.FunctionCall().name + "' expects up to " + std::to_string(num_args) + " argument(s)");
        }
        
        node_ptr var = tc_node(node->_Node.FunctionCall().args[0]);

        if (node->_Node.FunctionCall().args.size() == 2) {
            if (node->_Node.FunctionCall().args[1]->type != NodeType::NUMBER) {
                return throw_error("Function " + node->_Node.FunctionCall().name + " expects second argument to be a number");
            }
        }

        switch(var->type) {
            case NodeType::NONE: new_number_node(0);
            case NodeType::NUMBER: return var;
            case NodeType::STRING: {
                if (var->TypeInfo.is_type) {
                    node_ptr num_type = new_number_node(0);
                    num_type->TypeInfo.is_type = true;
                    return num_type;
                }
                std::string value = var->_Node.String().value;
                try {
                    if (node->_Node.FunctionCall().args.size() == 2) {
                        int base = node->_Node.FunctionCall().args[1]->_Node.Number().value;
                        return new_number_node(std::stol(value, nullptr, base));
                    }
                    return new_number_node(std::stod(value));
                } catch(...) {
                    return throw_error("Cannot convert \"" + value + "\" to a number");
                }
            };
            case NodeType::BOOLEAN: {
                if (var->_Node.Boolean().value) {
                    return new_number_node(1);
                }

                return new_number_node(0);
            };
            default: return new_number_node(0);
        }
    }

    if (node->_Node.FunctionCall().name == "type") {

        int num_args = 1;

        if (node->_Node.FunctionCall().args.size() != num_args) {
            return throw_error("Function '" + node->_Node.FunctionCall().name + "' expects " + std::to_string(num_args) + " argument(s)");
        }

        node_ptr var = tc_node(node->_Node.FunctionCall().args[0]);

        switch(var->type) {
            case NodeType::NONE: return new_string_node("None");
            case NodeType::NUMBER: return new_string_node("Number");
            case NodeType::STRING: return new_string_node("String");
            case NodeType::BOOLEAN: return new_string_node("Boolean");
            case NodeType::LIST: return new_string_node("List");
            case NodeType::OBJECT: return new_string_node("Object");
            case NodeType::FUNC: return new_string_node("Function");
            case NodeType::POINTER: return new_string_node("Pointer");
            case NodeType::LIB: return new_string_node("Library");
            case NodeType::ANY: return new_string_node("Any");
            case NodeType::ERROR: return new_string_node("Error");
            default: return new_string_node("None");
        }
    }

    if (node->_Node.FunctionCall().name == "evals") {
        
        int num_args = 1;

        if (node->_Node.FunctionCall().args.size() != num_args) {
            return throw_error("Function '" + node->_Node.FunctionCall().name + "' expects " + std::to_string(num_args) + " argument(s)");
        }

        node_ptr var = tc_node(node->_Node.FunctionCall().args[0]);

        if (var->type != NodeType::STRING) {
            return throw_error("Function " + node->_Node.FunctionCall().name + " expects 1 string argument");
        }

        return new_node(NodeType::NONE);
    }

    if (node->_Node.FunctionCall().name == "eval") {

        int num_args = 1;

        if (node->_Node.FunctionCall().args.size() != num_args) {
            return throw_error("Function '" + node->_Node.FunctionCall().name + "' expects " + std::to_string(num_args) + " argument(s)");
        }

        node_ptr var = tc_node(node->_Node.FunctionCall().args[0]);

        if (var->type != NodeType::STRING) {
            return throw_error("Function " + node->_Node.FunctionCall().name + " expects 1 string argument");
        }

        return new_node(NodeType::ANY);
    }

    if (node->_Node.FunctionCall().name == "load_lib") {
        return tc_load_lib(node);
    }

    if (node->_Node.FunctionCall().name == "exit") {
        return new_node(NodeType::NOVALUE);
    }

    if (func != nullptr) {
        function = copy_function(func);
    } 
    else if (!node->_Node.FunctionCall().caller) {
        node_ptr function_symbol = get_symbol(node->_Node.FunctionCall().name, current_scope);
        if (!function_symbol) {
            return throw_error("Function '" + node->_Node.FunctionCall().name + "' is undefined");
        }
        if (function_symbol->type != NodeType::FUNC) {
            return throw_error("Variable '" + node->_Node.FunctionCall().name + "' is not a function");
        }

        function = copy_function(function_symbol);

    } else {
        node_ptr method = node->_Node.FunctionCall().caller->_Node.Object().properties[node->_Node.FunctionCall().name];
        if (!method) {
            return throw_error("Method '" + node->_Node.FunctionCall().name + "' does not exist");
        }
        if (method->type != NodeType::FUNC) {
            return throw_error("Variable '" + node->_Node.FunctionCall().name + "' is not a function");
        }

        function = copy_function(method);
    }

    std::vector<node_ptr> args;
    for (node_ptr& arg : node->_Node.FunctionCall().args) {
        args.push_back(tc_node(arg));
    }

    // Type functions cannot be multiple dispatch, so we just call it here

    if (function->_Node.Function().type_function) {
        // We want to inject the type we pass in as an argument
        // To the param_types

        node_ptr func_call = new_node(NodeType::FUNC_CALL);

        // Inject any defaults

        if (args.size() < function->_Node.Function().params.size()) {
            for (auto& def : function->_Node.Function().default_values) {
                args.push_back(def.second);
            }
        }

        // Typecheck

        // If function param size does not match args size, skip
        if (function->_Node.Function().params.size() != args.size()) {
            return throw_error("Type function '" + function->_Node.Function().name + "' expects " + std::to_string(function->_Node.Function().params.size()) + " arguments but " + std::to_string(args.size()) + " were provided");
        }

        symt_ptr call_scope = std::make_shared<SymbolTable>();
        call_scope->file_name = node->_Node.FunctionCall().name;
        call_scope->parent = current_scope;
        current_scope = call_scope;

        Interpreter interp;
        interp.current_scope = call_scope;

        for (int i = 0; i < args.size(); i++) {
            node_ptr param = function->_Node.Function().params[i];
            node_ptr param_type = interp.eval_node(function->_Node.Function().param_types[param->_Node.ID().value]);
            if (param_type) {
                if (!match_types(param_type, args[i])) {
                    return throw_error("Type function '" + function->_Node.Function().name + "' expects argument '" + param->_Node.ID().value + "' to be of type '" + printable(param_type) + "'", node);
                }
            }
            func_call->_Node.FunctionCall().args.push_back(args[i]);
            add_symbol(param->_Node.ID().value, args[i], current_scope);
        }

        node_ptr result = interp.eval_function_direct(func_call, function);

        // Check against return type
        if (function->_Node.Function().return_type) {
            function->_Node.Function().return_type = tc_node(function->_Node.Function().return_type);
            if (!match_types(function->_Node.Function().return_type, result)) {
                node_ptr defined_ret_type = get_type(function->_Node.Function().return_type);
                node_ptr ret_type = get_type(result);
                return throw_error("Type Error in '" + function->_Node.Function().name + "': Return type '" + printable(ret_type) + "' does not match defined return type '" + printable(defined_ret_type) + "'");
            }
        } else {
            function->_Node.Function().return_type = result;
        }

        if (result->type == NodeType::FUNC && result->_Node.Function().type_function) {
            // We don't want to typecheck it, but we do want to inject scope into closure
            for (auto& symbol : current_scope->symbols) {
                result->_Node.Function().closure[symbol.first] = symbol.second;
            }
        }
        if (result->type == NodeType::FUNC && !result->_Node.Function().type_function) {
            result = tc_function(result);
        }

        current_scope = current_scope->parent;

        return result;
    }

    // Check if function args match any multiple dispatch functions

    auto functions = std::vector<node_ptr>(function->_Node.Function().dispatch_functions);
    functions.insert(functions.begin(), function);

    bool func_match = false;

    for (node_ptr& fx: functions) {

        // Inject defaults if args length < params length

        if (args.size() < fx->_Node.Function().params.size()) {
            for (auto& def : fx->_Node.Function().default_values) {
                args.push_back(def.second);
            }
        }

        // If function param size does not match args size, skip
        if (fx->_Node.Function().params.size() != args.size()) {
            continue;
        }

        for (int i = 0; i < args.size(); i++) {
            node_ptr param = fx->_Node.Function().params[i];
            node_ptr param_type = fx->_Node.Function().param_types[param->_Node.ID().value];
            if (param_type) {
                if (!match_types(param_type, args[i])) {
                    goto not_found;
                }
            } else {
                continue;
            }
        }

        // If we get here, we've found the function
        function = std::make_shared<Node>(*fx);
        func_match = true;
        break;

        not_found:
            continue;
    }

    if (!func_match) {
        std::string argsStr = "(";
        for (int i = 0; i < args.size(); i++) {
            node_ptr arg = get_type(args[i]);
            argsStr += printable(arg);
            if (i != args.size()-1) {
                argsStr += ", ";
            }
        }
        argsStr += ")";
        return throw_error("Dispatch error in function '" + node->_Node.FunctionCall().name + "' - No function found matching args: " + argsStr + "\n\nAvailable functions:\n\n" + printable(functions[0]));
    }

    auto local_scope = std::make_shared<SymbolTable>();
    auto curr_scope = std::make_shared<SymbolTable>();

    current_scope->child = local_scope;
    local_scope->parent = current_scope;

    local_scope->child = curr_scope;
    curr_scope->parent = local_scope;

    // current_scope 
    //    -> local_scope 
    //        -> curr_scope

    if (node->_Node.FunctionCall().caller != nullptr) {
        curr_scope->symbols["this"] = node->_Node.FunctionCall().caller;
    }

    current_scope = curr_scope;
    current_scope->file_name = function->_Node.Function().name + ": " + function->_Node.Function().decl_filename;
    
    // Inject closure into local scope

    for (auto& elem : function->_Node.Function().closure) {
        add_symbol(elem.first, elem.second, curr_scope);
    }

    int num_empty_args = std::count(function->_Node.Function().args.begin(), function->_Node.Function().args.end(), nullptr);

    if (args.size() > num_empty_args) {
        return throw_error("Function '" + node->_Node.FunctionCall().name + "' expects " + std::to_string(num_empty_args) + " parameters but " + std::to_string(args.size()) + " were provided");
    }

    for (int i = 0; i < args.size(); i++) {
        std::string name = function->_Node.Function().params[i]->_Node.ID().value;
        node_ptr value = args[i];
        add_symbol(name, value, curr_scope);
    }

    node_ptr res = function;

    // OnCall Hook
    if (function->Meta.onCallFunction) {

        node_ptr return_object = new_node(NodeType::OBJECT);

        return_object->_Node.Object().properties["name"] = new_string_node(function->_Node.Function().name);
        return_object->_Node.Object().properties["params"] = new_node(NodeType::LIST);
        return_object->_Node.Object().properties["args"] = new_node(NodeType::LIST);
        return_object->_Node.Object().properties["return"] = res;

        for (node_ptr& param : function->_Node.Function().params) {
            return_object->_Node.Object().properties["params"]->_Node.List().elements.push_back(new_string_node(param->_Node.ID().value));
        }

        for (node_ptr arg : args) {
            return_object->_Node.Object().properties["args"]->_Node.List().elements.push_back(arg);
        }

        node_ptr return_object_type = get_type(return_object);
        node_ptr onCallFunction = copy_function(function->Meta.onCallFunction);
        onCallFunction = tc_function(onCallFunction);

        // Typecheck first param
        std::string param_name = onCallFunction->_Node.Function().params[0]->_Node.ID().value;
        node_ptr param_type = onCallFunction->_Node.Function().param_types[param_name];

        if (!param_type) {
            onCallFunction->_Node.Function().param_types[param_name] = return_object_type;
        } else if (param_type->type == NodeType::ANY) {
            onCallFunction->_Node.Function().param_types[param_name] = return_object_type;
        } else {
            if (!match_types(return_object_type, param_type)) {
                return throw_error("OnCallFunction's first paramater is expected to be of type '" + printable(return_object_type) + "' but was typed as '" + printable(param_type) + "'");
            }
        }

        node_ptr func_call = new_node(NodeType::FUNC_CALL);
        func_call->_Node.FunctionCall().args.push_back(return_object);
        tc_func_call(func_call, onCallFunction);
    }

    current_scope = current_scope->parent->parent;
    current_scope->child->child = nullptr;
    current_scope->child = nullptr;

    if (res->_Node.Function().return_type) {
        return res->_Node.Function().return_type;
    } else {
        return new_node(NodeType::ANY);
    }

    return res;
}

node_ptr Typechecker::copy_function(node_ptr& func) {
    node_ptr function = new_node(NodeType::FUNC);
    function->_Node.Function().name = func->_Node.Function().name;
    function->_Node.Function().args = std::vector<node_ptr>(func->_Node.Function().args);
    function->_Node.Function().params = std::vector<node_ptr>(func->_Node.Function().params);
    function->_Node.Function().default_values = func->_Node.Function().default_values;
    function->_Node.Function().body = std::make_shared<Node>(*func->_Node.Function().body);
    function->_Node.Function().closure = func->_Node.Function().closure;
    function->_Node.Function().is_hook = func->_Node.Function().is_hook;
    function->_Node.Function().decl_filename = func->_Node.Function().decl_filename;
    function->Meta.onCallFunction = func->Meta.onCallFunction;
    function->_Node.Function().param_types = func->_Node.Function().param_types;
    function->_Node.Function().return_type = func->_Node.Function().return_type;
    function->_Node.Function().dispatch_functions = func->_Node.Function().dispatch_functions;
    function->_Node.Function().type_function = func->_Node.Function().type_function;
    function->TypeInfo = func->TypeInfo;
    function->Meta = func->Meta;
    return function;
}