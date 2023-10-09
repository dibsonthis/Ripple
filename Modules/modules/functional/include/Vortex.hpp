#pragma once

#include <memory>
#include <vector>
#include <unordered_map>
#include <string>
#include <iostream>

enum ValueType
{
    Number,
    String,
    Boolean,
    List,
    Type,
    Object,
    Function,
    Native,
    Pointer,
    None
};

struct Value;
struct Closure;

struct Chunk
{
    std::vector<uint8_t> code;
    std::vector<int> lines;
    std::vector<Value> constants;
    std::vector<std::string> variables;
};

struct ClosedVar
{
    std::string name;
    int index;
    bool is_local;
};

struct FunctionObj
{
    std::string name;
    int arity;
    int defaults;
    Chunk chunk;
    std::vector<int> instruction_offsets;
    std::vector<ClosedVar> closed_var_indexes;
    std::vector<std::shared_ptr<Closure>> closed_vars;
    std::vector<Value> default_values;
    std::shared_ptr<Value> object;
    std::vector<std::string> params;
    bool is_generator = false;
    bool generator_init = false;
    bool generator_done = false;
    bool is_type_generator = false;
};

struct TypeObj
{
    std::string name;
    std::unordered_map<std::string, Value> types;
    std::unordered_map<std::string, Value> defaults;
};

struct ObjectObj
{
    std::shared_ptr<TypeObj> type;
    std::unordered_map<std::string, Value> values;
    std::vector<std::string> keys;
    std::string type_name;
};

struct PointerObj
{
    void *value;
};

typedef Value (*NativeFunction)(std::vector<Value> &args);

struct NativeFunctionObj
{
    std::string name;
    NativeFunction function = nullptr;
};

struct Meta
{
    bool unpack = false;
    bool packer = false;
    bool is_const = false;
    bool temp_non_const = false;
};

struct ValueHooks
{
    std::shared_ptr<Value> onChangeHook = nullptr;
};

struct Value
{
    ValueType type;
    Meta meta;
    ValueHooks hooks;
    std::variant<
        double,
        std::string,
        bool,
        std::shared_ptr<std::vector<Value>>,
        std::shared_ptr<FunctionObj>,
        std::shared_ptr<TypeObj>,
        std::shared_ptr<ObjectObj>,
        std::shared_ptr<NativeFunctionObj>,
        std::shared_ptr<PointerObj>>
        value;

    Value() : type(None) {}
    Value(ValueType type) : type(type)
    {
        switch (type)
        {
        case Number:
            value = 0.0f;
            break;
        case String:
            value = "";
            break;
        case Boolean:
            value = false;
            break;
        case List:
            value = std::make_shared<std::vector<Value>>();
            break;
        case Type:
            value = std::make_shared<TypeObj>();
            break;
        case Object:
            value = std::make_shared<ObjectObj>();
            break;
        case Function:
            value = std::make_shared<FunctionObj>();
            break;
        case Native:
            value = std::make_shared<NativeFunctionObj>();
            break;
        case Pointer:
            value = std::make_shared<PointerObj>();
            break;
        default:
            break;
        }
    }

    double &get_number()
    {
        return std::get<double>(this->value);
    }
    std::string &get_string()
    {
        return std::get<std::string>(this->value);
    }
    bool &get_boolean()
    {
        return std::get<bool>(this->value);
    }

    std::shared_ptr<std::vector<Value>> &get_list()
    {
        return std::get<std::shared_ptr<std::vector<Value>>>(this->value);
    }

    std::shared_ptr<TypeObj> &get_type()
    {
        return std::get<std::shared_ptr<TypeObj>>(this->value);
    }

    std::shared_ptr<ObjectObj> &get_object()
    {
        return std::get<std::shared_ptr<ObjectObj>>(this->value);
    }

    std::shared_ptr<FunctionObj> &get_function()
    {
        return std::get<std::shared_ptr<FunctionObj>>(this->value);
    }

    std::shared_ptr<NativeFunctionObj> &get_native()
    {
        return std::get<std::shared_ptr<NativeFunctionObj>>(this->value);
    }

    std::shared_ptr<PointerObj> &get_pointer()
    {
        return std::get<std::shared_ptr<PointerObj>>(this->value);
    }

    bool is_number()
    {
        return type == Number;
    }
    bool is_string()
    {
        return type == String;
    }
    bool is_boolean()
    {
        return type == Boolean;
    }
    bool is_list()
    {
        return type == List;
    }
    bool is_type()
    {
        return type == Type;
    }
    bool is_object()
    {
        return type == Object;
    }
    bool is_function()
    {
        return type == Function;
    }
    bool is_native()
    {
        return type == Native;
    }
    bool is_pointer()
    {
        return type == Pointer;
    }
    bool is_none()
    {
        return type == None;
    }
};

struct Closure
{
    Value *location;
    Value closed;
};

Value new_val()
{
    return Value(None);
}

Value number_val(double value)
{
    Value val(Number);
    val.value = value;
    return val;
}

Value string_val(std::string value)
{
    Value val(String);
    val.value = value;
    return val;
}

Value boolean_val(bool value)
{
    Value val(Boolean);
    val.value = value;
    return val;
}

Value list_val()
{
    Value val(List);
    return val;
}

Value type_val(std::string name)
{
    Value val(Type);
    val.get_type()->name = name;
    return val;
}

Value object_val()
{
    Value val(Object);
    return val;
}

Value function_val()
{
    Value val(Function);
    return val;
}

Value native_val()
{
    Value val(Native);
    return val;
}

Value pointer_val()
{
    Value val(Pointer);
    return val;
}

Value none_val()
{
    Value val(None);
    return val;
}

void error(std::string message)
{
    std::cout << message << "\n";
    exit(1);
}

struct CallFrame
{
    std::string name;
    std::shared_ptr<FunctionObj> function;
    uint8_t *ip;
    int frame_start;
    int sp;
    int instruction_index;
    std::vector<Value> gen_stack;
};
struct VM
{
    std::vector<Value> stack;
    Value *sp;
    /* Possibly change to array with specified MAX_DEPTH */
    std::vector<CallFrame> frames;
    std::unordered_map<std::string, std::shared_ptr<CallFrame>> gen_frames;
    std::vector<Value *> objects;
    std::unordered_map<std::string, Value> globals;
    int status = 0;
    std::vector<std::shared_ptr<Closure>> closed_values;
    int coro_count = 0;

    VM()
    {
        stack.reserve(100000);
    }
};

static void runtimeError(VM &vm, std::string message, ...)
{

    vm.status = 1;

    CallFrame frame = vm.frames.back();

    va_list args;
    va_start(args, message);
    vfprintf(stderr, message.c_str(), args);
    va_end(args);
    fputs("\n", stderr);

    for (int i = vm.frames.size() - 1; i >= 0; i--)
    {
        CallFrame *frame = &vm.frames[i];
        auto &function = frame->function;
        size_t instruction = frame->ip - function->chunk.code.data() - 1;
        if (function->name == "error")
        {
            continue;
        }
        fprintf(stderr, "[line %d] in ",
                function->chunk.lines[instruction]);
        if (function->name == "")
        {
            std::string name = frame->name;
            if (name == "")
            {
                name = "script";
            }
            fprintf(stderr, "%s", (name + "\n").c_str());
        }
        else
        {
            fprintf(stderr, "%s()\n", function->name.c_str());
        }
    }
}