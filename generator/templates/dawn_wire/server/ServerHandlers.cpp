//* Copyright 2019 The Dawn Authors
//*
//* Licensed under the Apache License, Version 2.0 (the "License");
//* you may not use this file except in compliance with the License.
//* You may obtain a copy of the License at
//*
//*     http://www.apache.org/licenses/LICENSE-2.0
//*
//* Unless required by applicable law or agreed to in writing, software
//* distributed under the License is distributed on an "AS IS" BASIS,
//* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//* See the License for the specific language governing permissions and
//* limitations under the License.

#include "common/Assert.h"
#include "dawn_wire/server/Server.h"

namespace dawn_wire { namespace server {
    {% for command in cmd_records["command"] %}
        {% set type = command.derived_object %}
        {% set method = command.derived_method %}
        {% set is_method = method != None %}
        {% set returns = is_method and method.return_type.name.canonical_case() != "void" %}

        {% set Suffix = command.name.CamelCase() %}
        {% if Suffix not in client_side_commands %}
            //* The generic command handlers
            bool Server::Handle{{Suffix}}(const char** commands, size_t* size) {
                {{Suffix}}Cmd cmd;
                DeserializeResult deserializeResult = cmd.Deserialize(commands, size, &mAllocator
                    {%- if command.has_dawn_object -%}
                        , *this
                    {%- endif -%}
                );

                if (deserializeResult == DeserializeResult::FatalError) {
                    return false;
                }

                {% if Suffix in server_custom_pre_handler_commands %}
                    if (!PreHandle{{Suffix}}(cmd)) {
                        return false;
                    }
                {% endif %}

                {% if is_method %}
                    //* Unpack 'self'
                    auto* selfData = {{type.name.CamelCase()}}Objects().Get(cmd.selfId);
                    ASSERT(selfData != nullptr);
                {% endif %}

                //* Allocate any result objects
                {%- for member in command.members if member.is_return_value -%}
                    {{ assert(member.handle_type) }}
                    {% set Type = member.handle_type.name.CamelCase() %}
                    {% set name = as_varName(member.name) %}

                    auto* {{name}}Data = {{Type}}Objects().Allocate(cmd.{{name}}.id);
                    if ({{name}}Data == nullptr) {
                        return false;
                    }
                    {{name}}Data->serial = cmd.{{name}}.serial;

                    {% if type.is_builder %}
                        selfData->builtObject = cmd.{{name}};
                    {% endif %}
                {% endfor %}

                //* After the data is allocated, apply the argument error propagation mechanism
                if (deserializeResult == DeserializeResult::ErrorObject) {
                    {% if type.is_builder %}
                        selfData->valid = false;
                        //* If we are in GetResult, fake an error callback
                        {% if returns %}
                            On{{type.name.CamelCase()}}Error(DAWN_BUILDER_ERROR_STATUS_ERROR, "Maybe monad", cmd.selfId, selfData->serial);
                        {% endif %}
                    {% endif %}
                    return true;
                }

                //* Do command
                bool success = Do{{Suffix}}(
                    {%- for member in command.members -%}
                        {%- if member.is_return_value -%}
                            {%- if member.handle_type -%}
                                &{{as_varName(member.name)}}Data->handle //* Pass the handle of the output object to be written by the doer
                            {%- else -%}
                                &cmd.{{as_varName(member.name)}}
                            {%- endif -%}
                        {%- else -%}
                            cmd.{{as_varName(member.name)}}
                        {%- endif -%}
                        {%- if not loop.last -%}, {% endif %}
                    {%- endfor -%}
                );

                //* Mark output object handles as valid/invalid
                {%- for member in command.members if member.is_return_value and member.handle_type -%}
                    {% set name = as_varName(member.name) %}
                    {{name}}Data->valid = {{name}}Data->handle != nullptr;
                {% endfor %}

                if (!success) {
                    return false;
                }

                {%- for member in command.members if member.is_return_value and member.handle_type -%}
                    {% set Type = member.handle_type.name.CamelCase() %}
                    {% set name = as_varName(member.name) %}

                    {% if Type in server_reverse_lookup_objects %}
                        //* For created objects, store a mapping from them back to their client IDs
                        if ({{name}}Data->valid) {
                            {{Type}}ObjectIdTable().Store({{name}}Data->handle, cmd.{{name}}.id);
                        }
                    {% endif %}

                    //* builders remember the ID of the object they built so that they can send it
                    //* in the callback to the client.
                    {% if member.handle_type.is_builder %}
                        if ({{name}}Data->valid) {
                            uint64_t userdata1 = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(this));
                            uint64_t userdata2 = (uint64_t({{name}}Data->serial) << uint64_t(32)) + cmd.{{name}}.id;
                            mProcs.{{as_varName(member.handle_type.name, Name("set error callback"))}}({{name}}Data->handle, Forward{{Type}}, userdata1, userdata2);
                        }
                    {% endif %}
                {% endfor %}

                return true;
            }
        {% endif %}
    {% endfor %}

    const char* Server::HandleCommands(const char* commands, size_t size) {
        mProcs.deviceTick(DeviceObjects().Get(1)->handle);

        while (size >= sizeof(WireCmd)) {
            WireCmd cmdId = *reinterpret_cast<const WireCmd*>(commands);

            bool success = false;
            switch (cmdId) {
                {% for command in cmd_records["command"] %}
                    case WireCmd::{{command.name.CamelCase()}}:
                        success = Handle{{command.name.CamelCase()}}(&commands, &size);
                        break;
                {% endfor %}
                default:
                    success = false;
            }

            if (!success) {
                return nullptr;
            }
            mAllocator.Reset();
        }

        if (size != 0) {
            return nullptr;
        }

        return commands;
    }

}}  // namespace dawn_wire::server
