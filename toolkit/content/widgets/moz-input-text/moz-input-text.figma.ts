import figma, { html } from "@figma/code-connect/html";

// Desktop v3 (newest)
figma.connect(
  "https://www.figma.com/design/3WoKOSGtaSjhUHKldHCXbc/Desktop-Components-3?node-id=33-302",
  {
    props: {
      labelProps: figma.nestedProps("Label", {
        description: figma.boolean("Show description", {
          true: figma.string("Description"),
        }),
        label: figma.string("Label"),
        supportPage: figma.boolean("Show support link", {
          true: "sumo-slug",
        }),
        iconSrc: figma.boolean("Show icon", {
          true: "chrome://example.svg",
        }),
        showSlot: figma.boolean("Show slot", {
          true: figma.instance("Slot"),
        }),
      }),
      textInputProps: figma.nestedProps("Text input", {
        placeholder: figma.boolean("Show placeholder", {
          true: figma.string("Placeholder"),
        }),
        text: figma.string("Text"),
        // iconSrc: figma.boolean("Show icon", {
        //   true: "chrome://example.svg", // not yet implemented in the design system
        // }),
      }),
      errorMessage: figma.enum("State", {
        // Error: figma.string("Error message"), // not yet implemented in the design system
        Error: html` <!--
  The error message functionality is not currently
  available with design system components,
  and will need to be implemented in context for now.
-->`,
      }),
      disabled: figma.enum("State", { Disabled: true }),
    },
    example: props => html`
      <moz-input-text
        value=${props.textInputProps.text}
        label=${props.labelProps.label}
        description=${props.labelProps.description}
        support-page=${props.labelProps.supportPage}
        iconsrc=${props.labelProps.iconSrc}
        placeholder=${props.textInputProps.placeholder}
        disabled=${props.disabled}
      >
        ${props.labelProps.showSlot}
      </moz-input-text>
      ${props.errorMessage}
    `,
  }
);
