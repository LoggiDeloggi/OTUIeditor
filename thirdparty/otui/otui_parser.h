#ifndef OTUI_EDITOR_OTUI_PARSER_H
#define OTUI_EDITOR_OTUI_PARSER_H

#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct OTUIProp {
    char* key;
    char* value;
    char* comment;           // Inline comment after property
} OTUIProp;

typedef struct OTUIState {
    char* condition;         // State condition (hover, pressed, disabled, etc)
    bool negated;            // true if $!condition
    OTUIProp* props;         // Properties within this state
    size_t nprops;
    size_t cprops;
} OTUIState;

typedef struct OTUIEvent {
    char* name;              // Event name (onClick, onEnter, etc)
    char* code;              // Lua code (can be multi-line)
    bool multiline;          // true if uses | for multi-line
} OTUIEvent;

typedef struct OTUINode {
    char* name;              // Widget/style name or tag
    char* base_style;        // Base style name if inherits (WidgetName < BaseStyle)
    int indent;              // indentation level (spaces count)
    char* comment_before;    // Comment line(s) before this node
    char* comment_inline;    // Inline comment after node name/base_style
    OTUIProp* props;         // key:value pairs
    size_t nprops;
    size_t cprops;
    OTUIState* states;       // Conditional states ($hover, $pressed, etc)
    size_t nstates;
    size_t cstates;
    OTUIEvent* events;       // Inline events (@onClick, @onEnter, etc)
    size_t nevents;
    size_t cevents;
    struct OTUINode** children;
    size_t nchildren;
    size_t cchildren;
} OTUINode;

// Parse OTUI/OTML-like file into a node tree. Returns NULL on error.
OTUINode* otui_parse_file(const char* filepath, char* errbuf, size_t errsz);
void otui_free(OTUINode* node);

// Helpers
const char* otui_prop_get(const OTUINode* node, const char* key);
void otui_dump(const OTUINode* node, int depth, FILE* out);

// Define ou substitui uma propriedade (retorna 1 em sucesso, 0 falha)
int otui_prop_set(OTUINode* node, const char* key, const char* value);

// Serializa a árvore em arquivo .otui.
// Retorna 1 em sucesso, 0 em falha (errbuf preenchido se fornecido).
int otui_save(const OTUINode* root, const char* filepath, char* errbuf, size_t errsz);

// Cria e adiciona um filho com nome especificado ao nó parent. Retorna ponteiro ou NULL.
OTUINode* otui_node_add_child(OTUINode* parent, const char* name);
// Remove filho; retorna 1 se encontrado/removido, 0 caso contrário.
int otui_node_remove_child(OTUINode* parent, OTUINode* child);

// Utilidades para undo/redo
// Destaca child de parent sem liberar memória; outIndex recebe a posição original
int otui_node_detach_child(OTUINode* parent, OTUINode* child, size_t* outIndex);
// Insere child em parent na posição index; não duplica child
int otui_node_insert_child(OTUINode* parent, OTUINode* child, size_t index);

// Z-order controls
// Move child um nivel para frente (aumenta indice em 1); retorna 1 se movido, 0 se ja era o ultimo
int otui_node_bring_forward(OTUINode* parent, OTUINode* child);
// Move child um nivel para tras (diminui indice em 1); retorna 1 se movido, 0 se ja era o primeiro
int otui_node_send_backward(OTUINode* parent, OTUINode* child);

// Style inheritance
// Find node by name recursively in tree
OTUINode* otui_find_node(OTUINode* root, const char* name);
// Resolve style inheritance for a node (merge base_style properties)
void otui_resolve_inheritance(OTUINode* node, OTUINode* root);
// Resolve inheritance for entire tree
void otui_resolve_all_inheritance(OTUINode* root);

// State helpers
OTUIState* otui_state_get(const OTUINode* node, const char* condition);
const char* otui_state_prop_get(const OTUIState* state, const char* key);

// Event helpers
OTUIEvent* otui_event_get(const OTUINode* node, const char* name);

#ifdef __cplusplus
}
#endif

#endif // OTUI_EDITOR_OTUI_PARSER_H
