/**
 * @file sse_parser_config.h
 * @brief Private configuration of the sse_parser component.
 */
#ifndef SSE_PARSER_CONFIG_H
#define SSE_PARSER_CONFIG_H

/** Parsers available at the same time. http_client keeps a single stream open. */
#define SSE_PARSER_POOL_SIZE 1U

/** SSE field prefixes (https://html.spec.whatwg.org/multipage/server-sent-events.html). */
#define SSE_PARSER_FIELD_EVENT "event:"
#define SSE_PARSER_FIELD_DATA  "data:"

#endif /* SSE_PARSER_CONFIG_H */
