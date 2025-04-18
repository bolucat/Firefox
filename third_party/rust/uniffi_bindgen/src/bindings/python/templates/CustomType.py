{%- match python_config.custom_types.get(name.as_str())  %}
{% when None %}
{#- No custom type config, just forward all methods to our builtin type #}
class _UniffiConverterType{{ name }}:
    @staticmethod
    def write(value, buf):
        {{ builtin|ffi_converter_name }}.write(value, buf)

    @staticmethod
    def read(buf):
        return {{ builtin|ffi_converter_name }}.read(buf)

    @staticmethod
    def lift(value):
        return {{ builtin|ffi_converter_name }}.lift(value)

    @staticmethod
    def check_lower(value):
        return {{ builtin|ffi_converter_name }}.check_lower(value)

    @staticmethod
    def lower(value):
        return {{ builtin|ffi_converter_name }}.lower(value)

{%- when Some(config) %}

{%- if let Some(imports) = config.imports %}
{%- for import_name in imports %}
{{ self.add_import(import_name) }}
{%- endfor %}
{%- endif %}

{#- Custom type config supplied, use it to convert the builtin type #}
class _UniffiConverterType{{ name }}:
    @staticmethod
    def write(value, buf):
        builtin_value = {{ config.lower("value") }}
        {{ builtin|write_fn }}(builtin_value, buf)

    @staticmethod
    def read(buf):
        builtin_value = {{ builtin|read_fn }}(buf)
        return {{ config.lift("builtin_value") }}

    @staticmethod
    def lift(value):
        builtin_value = {{ builtin|lift_fn }}(value)
        return {{ config.lift("builtin_value") }}

    @staticmethod
    def check_lower(value):
        builtin_value = {{ config.lower("value") }}
        return {{ builtin|check_lower_fn }}(builtin_value)

    @staticmethod
    def lower(value):
        builtin_value = {{ config.lower("value") }}
        return {{ builtin|lower_fn }}(builtin_value)
{%- endmatch %}
