import figma, { html } from "@figma/code-connect/html";

// Desktop Components v3 (newest)
figma.connect(
  "https://www.figma.com/design/3WoKOSGtaSjhUHKldHCXbc/Desktop-Components-3?node-id=478-7070",
  {
    props: {
      items: figma.children("*"),
    },
    example: props =>
      html` <panel-list open id="panel-list"> ${props.items} </panel-list>`,
  }
);

figma.connect(
  "https://www.figma.com/design/3WoKOSGtaSjhUHKldHCXbc/Desktop-Components-3?node-id=478-6518",
  {
    props: {
      label: figma.string("Label"),
      iconSrc: figma.boolean("Show icon", {
        true: "chrome://example.svg",
      }),
      disabled: figma.enum("State", { Disabled: true }),
      badged: figma.boolean("Show badge"),
      submenu: figma.boolean("Show submenu"),
      rule: figma.boolean("Show separator", {
        true: "<hr />",
      }),
    },
    example: props =>
      html` <panel-item
          icon=${props.iconSrc}
          disabled=${props.disabled}
          badged=${props.badged}
          submenu=${props.submenu}
        >
          ${props.label}
        </panel-item>
        ${props.rule}`,
  }
);
